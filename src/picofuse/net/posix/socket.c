#include "../../sys/iostream/iostream.h"
#include "posix.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#ifndef NET_CONN_BUFFER_SIZE
#define NET_CONN_BUFFER_SIZE 512
#endif

// Per-datagram cap for a "connected" UDP stream's receive queue - large
// enough for a typical Ethernet-path MTU's worth of payload. A datagram
// larger than this is truncated the same way a too-small recv() buffer
// would truncate a real UDP socket's read.
#ifndef NET_CONN_UDP_DGRAM_MAX_SIZE
#define NET_CONN_UDP_DGRAM_MAX_SIZE 1500
#endif

// How many not-yet-fully-read datagrams a "connected" UDP stream
// (net_open()) buffers before dropping further arrivals - UDP has no
// flow control, so this is a best-effort cap, not backpressure.
#ifndef NET_CONN_UDP_QUEUE_CAPACITY
#define NET_CONN_UDP_QUEUE_CAPACITY 4
#endif

// How often the background RX thread re-checks whether it should keep
// running, between otherwise-blocking poll() calls - same convention as
// hw/posix/uart.c's own HW_UART_POLL_TIMEOUT_MS.
#define NET_POLL_TIMEOUT_MS 100

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef struct {
  char data[NET_CONN_UDP_DGRAM_MAX_SIZE];
  size_t len;
} _net_conn_dgram_t;

// Heap-allocated per-connection context - net_open()'s socket and each TCP
// connection listener.c's net_listener_init() accepts, both backed by a
// real, ongoing socket with a background thread feeding an RX buffer for
// sys_iostream_read()/readiness callbacks. Same pattern as
// hw/posix/uart.c's own _hw_uart_ctx_t.
//
// TCP has no message boundaries - rx.tcp is a plain byte ring, same as
// hw/posix/uart.c's own. UDP does: rx.udp is a queue of whole, separate
// datagrams, so a single sys_iostream_read() call (or a run of them before
// the current datagram is exhausted) never mixes bytes from two different
// datagrams together - see _net_conn_ops_read()'s own doc.
typedef struct {
  int fd;
  // Clear to ask the background thread to exit - a real sys_atomic_t, not
  // a plain bool, for the same cross-thread-visibility reason
  // hw/posix/uart.c's own _hw_uart_ctx_t.running is.
  sys_atomic_t running;
  sys_waitgroup_t *wg; // signaled by the thread just before it exits
  sys_mutex_t *lock;   // guards everything below, shared with the thread
  sys_iostream_t *stream;
  net_proto_t proto;
  // sys_iostream_peek()'s read-then-undo contract - see
  // hw/posix/uart.c's own last_byte/pushback for the same trick. Note:
  // for a UDP stream, peeking the *last* byte of a datagram and then
  // reading again can still glue it to the next datagram's first byte -
  // an accepted, narrow limitation of retrofitting a 1-byte-undo peek
  // onto datagram semantics (see _net_conn_ops_read()'s own doc).
  int last_byte;
  int pushback;
  union {
    struct {
      char buf[NET_CONN_BUFFER_SIZE];
      size_t read, write, count;
    } tcp;
    struct {
      _net_conn_dgram_t queue[NET_CONN_UDP_QUEUE_CAPACITY];
      size_t head, count;   // circular queue over queue[]
      size_t front_offset;  // bytes already consumed from queue[head]
    } udp;
  } rx;
} _net_conn_ctx_t;

///////////////////////////////////////////////////////////////////////////////
// ADDRESS CONVERSION

bool _net_addr_to_sockaddr(const net_addr_t *addr, uint16_t port,
                           struct sockaddr_storage *out, socklen_t *out_len) {
  memset(out, 0, sizeof(*out));
  if (addr->family == net_addr_family_v4) {
    struct sockaddr_in *sin = (struct sockaddr_in *)out;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    memcpy(&sin->sin_addr, addr->addr.v4, sizeof(addr->addr.v4));
    *out_len = sizeof(*sin);
    return true;
  }
  if (addr->family == net_addr_family_v6) {
    struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)out;
    sin6->sin6_family = AF_INET6;
    sin6->sin6_port = htons(port);
    memcpy(&sin6->sin6_addr, addr->addr.v6, sizeof(addr->addr.v6));
    *out_len = sizeof(*sin6);
    return true;
  }
  return false;
}

void _net_sockaddr_to_addr(const struct sockaddr_storage *sa,
                           net_addr_t *addr, uint16_t *port) {
  if (sa->ss_family == AF_INET) {
    const struct sockaddr_in *sin = (const struct sockaddr_in *)sa;
    *addr = net_addr_v4(0, 0, 0, 0);
    memcpy(addr->addr.v4, &sin->sin_addr, sizeof(addr->addr.v4));
    if (port != NULL) {
      *port = ntohs(sin->sin_port);
    }
  } else {
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
    *addr = net_addr_v6_any();
    memcpy(addr->addr.v6, &sin6->sin6_addr, sizeof(addr->addr.v6));
    if (port != NULL) {
      *port = ntohs(sin6->sin6_port);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// CONNECTED STREAM (net_open(), and each TCP connection listener.c accepts)

static void _net_conn_rx_thread(void *arg) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)arg;
  struct pollfd pfd = {.fd = ctx->fd, .events = POLLIN, .revents = 0};

  while (sys_atomic_get(&ctx->running) != 0) {
    int res = poll(&pfd, 1, NET_POLL_TIMEOUT_MS);
    if (res < 0) {
      if (errno == EINTR) {
        continue;
      }
      sys_debugf("net", "conn_rx_thread: poll error on fd=%d, stopping",
                ctx->fd);
      break;
    }
    if (res == 0) {
      continue; // timeout - just recheck ctx->running
    }

    // Sized for a whole UDP datagram in one recv() call - a smaller
    // buffer would silently truncate a real datagram right here (recv()
    // on a SOCK_DGRAM discards whatever doesn't fit), before
    // NET_CONN_UDP_DGRAM_MAX_SIZE's own truncation-on-store below even
    // gets a say. Harmless for TCP either way - just a bigger chunk size.
    char buf[NET_CONN_UDP_DGRAM_MAX_SIZE];
    ssize_t got = recv(ctx->fd, buf, sizeof(buf), 0);
    if (got < 0) {
      if (errno == EINTR) {
        continue;
      }
      sys_debugf("net", "conn_rx_thread: recv error on fd=%d, stopping",
                ctx->fd);
      break;
    }
    if (got == 0) {
      sys_debugf("net", "conn_rx_thread: peer closed fd=%d, stopping",
                ctx->fd);
      break;
    }

    sys_mutex_lock(ctx->lock);
    if (ctx->proto == net_proto_udp) {
      // One recv() is one whole datagram (or a truncated one, if it was
      // bigger than our own buf above) - store it as a single queue
      // entry, never appended to anything else, so it can never merge
      // with another datagram. Silently dropped if the queue is already
      // full - no flow control on UDP, matching NET_CONN_UDP_QUEUE_CAPACITY's
      // own doc.
      if (ctx->rx.udp.count < NET_CONN_UDP_QUEUE_CAPACITY) {
        size_t slot =
            (ctx->rx.udp.head + ctx->rx.udp.count) % NET_CONN_UDP_QUEUE_CAPACITY;
        size_t copy_len = (size_t)got;
        if (copy_len > NET_CONN_UDP_DGRAM_MAX_SIZE) {
          copy_len = NET_CONN_UDP_DGRAM_MAX_SIZE;
        }
        memcpy(ctx->rx.udp.queue[slot].data, buf, copy_len);
        ctx->rx.udp.queue[slot].len = copy_len;
        ctx->rx.udp.count++;
      }
    } else {
      for (ssize_t i = 0; i < got && ctx->rx.tcp.count < NET_CONN_BUFFER_SIZE;
          i++) {
        ctx->rx.tcp.buf[ctx->rx.tcp.write] = buf[i];
        ctx->rx.tcp.write = (ctx->rx.tcp.write + 1) % NET_CONN_BUFFER_SIZE;
        ctx->rx.tcp.count++;
      }
    }
    // Snapshot callback/userdata together under the same lock
    // _net_conn_ops_set_callback() writes them under, then call out to
    // user code only after releasing it - same reasoning as
    // hw/posix/uart.c's own _hw_uart_rx_thread().
    sys_iostream_callback_t callback = ctx->stream->backend.net.callback;
    void *userdata = ctx->stream->backend.net.userdata;
    sys_mutex_unlock(ctx->lock);

    if (callback != NULL) {
      callback(ctx->stream, sys_iostream_event_read, userdata);
    }
  }

  // Notify once more, whatever broke the loop above (peer closed, or a
  // poll/recv error) - the standard "readable, but read() returns 0"
  // idiom for EOF, letting the app notice a dead connection instead of
  // this thread just quietly vanishing. Snapshot everything needed into
  // locals and signal sys_waitgroup_done() *before* invoking the
  // callback: the natural thing for it to do is call
  // sys_iostream_close() synchronously, and that call's own
  // sys_waitgroup_wait() must not block on this very thread reaching
  // sys_waitgroup_done() further down - so it has to already be done.
  // ctx must not be touched again after that point, since close() may
  // free it before this function returns.
  sys_mutex_lock(ctx->lock);
  sys_iostream_t *stream = ctx->stream;
  sys_iostream_callback_t callback = stream->backend.net.callback;
  void *userdata = stream->backend.net.userdata;
  sys_mutex_unlock(ctx->lock);

  sys_atomic_set(&ctx->running, 0);
  sys_waitgroup_done(ctx->wg);

  if (callback != NULL) {
    callback(stream, sys_iostream_event_read, userdata);
  }
}

// Reads buffered data. For TCP (a plain byte stream, no message
// boundaries to preserve) this drains ctx->rx.tcp same as
// hw/posix/uart.c's own ring buffer always has. For UDP, this only ever
// returns bytes from a single datagram per call: it drains
// ctx->rx.udp.queue[head] until that entry is fully consumed and then
// stops (even if the caller's buffer has room left and another datagram
// is already queued behind it) - the actual guarantee this exists for is
// that one sys_iostream_read() (or a contiguous run of them before the
// current datagram is exhausted) can never glue two separate datagrams'
// bytes together. A caller whose buffer is smaller than one datagram
// simply gets the rest of it on a follow-up call, rather than losing
// data - unlike a real recv() on a SOCK_DGRAM socket, which would discard
// whatever didn't fit; this behaves more like TCP in that one respect,
// while still never mixing datagrams.
static size_t _net_conn_ops_read(sys_iostream_t *s, char *buf, size_t n) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  if (n == 0) {
    return 0;
  }

  sys_mutex_lock(ctx->lock);
  size_t read_n = 0;
  if (ctx->pushback >= 0) {
    buf[read_n++] = (char)(uint8_t)ctx->pushback;
    ctx->last_byte = ctx->pushback;
    ctx->pushback = -1;
  }

  if (ctx->proto == net_proto_udp) {
    while (read_n < n && ctx->rx.udp.count > 0) {
      _net_conn_dgram_t *front = &ctx->rx.udp.queue[ctx->rx.udp.head];
      size_t avail = front->len - ctx->rx.udp.front_offset;
      size_t want = n - read_n;
      size_t copy_len = (want < avail) ? want : avail;
      memcpy(buf + read_n, front->data + ctx->rx.udp.front_offset, copy_len);
      ctx->rx.udp.front_offset += copy_len;
      read_n += copy_len;
      if (copy_len > 0) {
        ctx->last_byte = (uint8_t)buf[read_n - 1];
      }
      if (ctx->rx.udp.front_offset >= front->len) {
        ctx->rx.udp.head = (ctx->rx.udp.head + 1) % NET_CONN_UDP_QUEUE_CAPACITY;
        ctx->rx.udp.count--;
        ctx->rx.udp.front_offset = 0;
      }
      break; // never span into a second datagram within one call
    }
  } else {
    while (read_n < n && ctx->rx.tcp.count > 0) {
      uint8_t byte = (uint8_t)ctx->rx.tcp.buf[ctx->rx.tcp.read];
      ctx->rx.tcp.read = (ctx->rx.tcp.read + 1) % NET_CONN_BUFFER_SIZE;
      ctx->rx.tcp.count--;
      buf[read_n++] = (char)byte;
      ctx->last_byte = byte;
    }
  }
  sys_mutex_unlock(ctx->lock);
  return read_n;
}

static size_t _net_conn_ops_write(sys_iostream_t *s, const char *buf,
                                  size_t n) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  ssize_t written = send(ctx->fd, buf, n, 0);
  return written > 0 ? (size_t)written : 0;
}

// The only seek this stream supports is sys_iostream_peek()'s own "undo
// the single byte I just read" pattern - see hw/posix/uart.c's own
// _hw_uart_ops_seek().
static ptrdiff_t _net_conn_ops_seek(sys_iostream_t *s, ptrdiff_t offset,
                                    bool abs) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  if (abs || offset != -1) {
    return -1;
  }
  sys_mutex_lock(ctx->lock);
  bool ok = ctx->last_byte >= 0;
  if (ok) {
    ctx->pushback = ctx->last_byte;
    ctx->last_byte = -1;
  }
  sys_mutex_unlock(ctx->lock);
  return ok ? 0 : -1;
}

static bool _net_conn_ops_set_callback(sys_iostream_t *s,
                                       sys_iostream_callback_t callback,
                                       void *userdata) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  sys_mutex_lock(ctx->lock);
  s->backend.net.userdata = userdata;
  s->backend.net.callback = callback;
  sys_mutex_unlock(ctx->lock);
  return true;
}

static void _net_conn_ops_close(sys_iostream_t *s) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  sys_atomic_set(&ctx->running, 0);
  sys_waitgroup_wait(ctx->wg);
  sys_waitgroup_deinit(ctx->wg);
  sys_mutex_deinit(ctx->lock);
  close(ctx->fd);
  sys_free(ctx);
}

static const sys_iostream_ops_t _net_conn_ops = {
    .read = _net_conn_ops_read,
    .write = _net_conn_ops_write,
    .seek = _net_conn_ops_seek,
    .set_callback = _net_conn_ops_set_callback,
    .close = _net_conn_ops_close,
};

sys_iostream_t *_net_wrap_connected_fd(int fd, net_proto_t proto) {
  _net_conn_ctx_t *ctx = sys_calloc(1, sizeof(*ctx));
  if (ctx == NULL) {
    close(fd);
    return NULL;
  }
  ctx->fd = fd;
  ctx->proto = proto;
  ctx->last_byte = -1;
  ctx->pushback = -1;
  ctx->lock = sys_mutex_init();
  ctx->wg = sys_waitgroup_init();
  if (ctx->lock == NULL || ctx->wg == NULL) {
    sys_mutex_deinit(ctx->lock);
    sys_waitgroup_deinit(ctx->wg);
    close(fd);
    sys_free(ctx);
    return NULL;
  }

  sys_iostream_t *stream = _sys_iostream_alloc(&_net_conn_ops);
  if (stream == NULL) {
    sys_mutex_deinit(ctx->lock);
    sys_waitgroup_deinit(ctx->wg);
    close(fd);
    sys_free(ctx);
    return NULL;
  }

  ctx->stream = stream;
  stream->backend.net.instance = ctx;
  stream->backend.net.callback = NULL;
  stream->backend.net.userdata = NULL;

  sys_atomic_init(&ctx->running, 1);
  if (!sys_waitgroup_add(ctx->wg, 1)) {
    stream->in_use = false; // release the pool slot directly - ops.close()
                            // would double-free ctx
    sys_mutex_deinit(ctx->lock);
    sys_waitgroup_deinit(ctx->wg);
    close(fd);
    sys_free(ctx);
    return NULL;
  }
  if (!sys_thread_create(_net_conn_rx_thread, ctx)) {
    sys_waitgroup_done(ctx->wg); // undo the add(1) above - the thread never
                                 // started to do it itself
    stream->in_use = false;
    sys_mutex_deinit(ctx->lock);
    sys_waitgroup_deinit(ctx->wg);
    close(fd);
    sys_free(ctx);
    return NULL;
  }

  return stream;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

sys_iostream_t *net_open(net_proto_t proto, const net_addr_t *addr,
                         uint16_t port) {
  if (addr == NULL) {
    return NULL;
  }

  int family = (addr->family == net_addr_family_v6) ? AF_INET6 : AF_INET;
  int type = (proto == net_proto_udp) ? SOCK_DGRAM : SOCK_STREAM;

  int fd = socket(family, type, 0);
  if (fd < 0) {
    return NULL;
  }

  struct sockaddr_storage sa;
  socklen_t sa_len;
  if (!_net_addr_to_sockaddr(addr, port, &sa, &sa_len) ||
      connect(fd, (struct sockaddr *)&sa, sa_len) != 0) {
    close(fd);
    return NULL;
  }

  return _net_wrap_connected_fd(fd, proto);
}
