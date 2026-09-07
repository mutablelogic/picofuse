#include "../../sys/iostream/iostream.h"
#include "posix.h"
#include <errno.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef NET_DGRAM_MAX_SIZE
#define NET_DGRAM_MAX_SIZE 1500
#endif

// How often the accept/recvfrom threads re-check whether they should keep
// running, between otherwise-blocking poll() calls.
#define NET_POLL_TIMEOUT_MS 100

///////////////////////////////////////////////////////////////////////////////
// TYPES

// A single UDP listener: net_listener_init() spawns exactly one background
// thread which recvfrom()s each incoming datagram into its own ephemeral
// context, then hands it to the accept callback as a single-shot stream -
// one read, an optional one write (sendto() back to the sender), then close.
typedef struct {
  int fd;
  struct sockaddr_storage remote;
  socklen_t remote_len;
  char payload[NET_DGRAM_MAX_SIZE];
  size_t length;
  size_t pos;
} _net_dgram_ctx_t;

struct net_listener_t {
  sys_atomic_t claimed; // pool-slot ownership - see _net_listener_alloc()
  net_proto_t proto;
  int fd;
  sys_atomic_t running;
  sys_waitgroup_t *wg;
  net_accept_callback_t callback;
  void *userdata;
};

static net_listener_t _net_listener_pool[NET_LISTENER_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// UDP DATAGRAM STREAM (single-shot: one read, one optional write, close)

static size_t _net_dgram_ops_read(sys_iostream_t *s, char *buf, size_t n) {
  _net_dgram_ctx_t *ctx = (_net_dgram_ctx_t *)s->backend.net.instance;
  size_t remain = ctx->length - ctx->pos;
  size_t read_n = (n < remain) ? n : remain;
  memcpy(buf, ctx->payload + ctx->pos, read_n);
  ctx->pos += read_n;
  return read_n;
}

static size_t _net_dgram_ops_write(sys_iostream_t *s, const char *buf,
                                   size_t n) {
  _net_dgram_ctx_t *ctx = (_net_dgram_ctx_t *)s->backend.net.instance;
  ssize_t written = sendto(ctx->fd, buf, n, 0,
                           (struct sockaddr *)&ctx->remote, ctx->remote_len);
  return written > 0 ? (size_t)written : 0;
}

static ptrdiff_t _net_dgram_ops_seek(sys_iostream_t *s, ptrdiff_t offset,
                                     bool abs) {
  (void)s;
  (void)offset;
  (void)abs;
  return -1; // datagram payload doesn't support peek/seek
}

static void _net_dgram_ops_close(sys_iostream_t *s) {
  _net_dgram_ctx_t *ctx = (_net_dgram_ctx_t *)s->backend.net.instance;
  sys_free(ctx);
}

static const sys_iostream_ops_t _net_dgram_ops = {
    .read = _net_dgram_ops_read,
    .write = _net_dgram_ops_write,
    .seek = _net_dgram_ops_seek,
    .set_callback = NULL, // single-shot - readiness callbacks make no sense
    .close = _net_dgram_ops_close,
};

///////////////////////////////////////////////////////////////////////////////
// POOL

static net_listener_t *_net_listener_alloc(void) {
  for (size_t i = 0; i < NET_LISTENER_CAPACITY; i++) {
    net_listener_t *listener = &_net_listener_pool[i];
    if (sys_atomic_inc(&listener->claimed) == 1) {
      return listener;
    }
    sys_atomic_dec(&listener->claimed);
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// ACCEPT THREADS

static void _net_listener_tcp_thread(void *arg) {
  net_listener_t *listener = (net_listener_t *)arg;
  struct pollfd pfd = {.fd = listener->fd, .events = POLLIN, .revents = 0};

  while (sys_atomic_get(&listener->running) != 0) {
    int res = poll(&pfd, 1, NET_POLL_TIMEOUT_MS);
    if (res < 0) {
      if (errno == EINTR) {
        continue;
      }
      sys_debugf("net", "listener_tcp_thread: poll error, stopping");
      break;
    }
    if (res == 0) {
      continue;
    }

    struct sockaddr_storage sa;
    socklen_t sa_len = sizeof(sa);
    int conn_fd = accept(listener->fd, (struct sockaddr *)&sa, &sa_len);
    if (conn_fd < 0) {
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      sys_debugf("net", "listener_tcp_thread: accept error, stopping");
      break;
    }

    sys_iostream_t *conn = _net_wrap_connected_fd(conn_fd, net_proto_tcp);
    if (conn == NULL) {
      continue;
    }

    net_addr_t remote;
    uint16_t remote_port;
    _net_sockaddr_to_addr(&sa, &remote, &remote_port);
    listener->callback(listener, conn, &remote, remote_port,
                       listener->userdata);
  }

  sys_atomic_set(&listener->running, 0);
  sys_waitgroup_done(listener->wg);
}

static void _net_listener_udp_thread(void *arg) {
  net_listener_t *listener = (net_listener_t *)arg;
  struct pollfd pfd = {.fd = listener->fd, .events = POLLIN, .revents = 0};

  while (sys_atomic_get(&listener->running) != 0) {
    int res = poll(&pfd, 1, NET_POLL_TIMEOUT_MS);
    if (res < 0) {
      if (errno == EINTR) {
        continue;
      }
      sys_debugf("net", "listener_udp_thread: poll error, stopping");
      break;
    }
    if (res == 0) {
      continue;
    }

    _net_dgram_ctx_t *ctx = sys_calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
      continue;
    }

    ctx->fd = listener->fd;
    ctx->remote_len = sizeof(ctx->remote);
    ssize_t got = recvfrom(listener->fd, ctx->payload, sizeof(ctx->payload),
                           0, (struct sockaddr *)&ctx->remote,
                           &ctx->remote_len);
    if (got < 0) {
      sys_free(ctx);
      if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
        continue;
      }
      sys_debugf("net", "listener_udp_thread: recvfrom error, stopping");
      break;
    }
    ctx->length = (size_t)got;

    sys_iostream_t *stream = _sys_iostream_alloc(&_net_dgram_ops);
    if (stream == NULL) {
      sys_free(ctx);
      continue;
    }
    stream->backend.net.instance = ctx;
    stream->backend.net.callback = NULL;
    stream->backend.net.userdata = NULL;

    net_addr_t remote;
    uint16_t remote_port;
    _net_sockaddr_to_addr(&ctx->remote, &remote, &remote_port);
    listener->callback(listener, stream, &remote, remote_port,
                       listener->userdata);
  }

  sys_atomic_set(&listener->running, 0);
  sys_waitgroup_done(listener->wg);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

net_listener_t *net_listener_init(net_proto_t proto, const net_addr_t *addr,
                                  uint16_t port, net_accept_callback_t callback,
                                  void *userdata) {
  if (addr == NULL || callback == NULL) {
    return NULL;
  }

  net_listener_t *listener = _net_listener_alloc();
  if (listener == NULL) {
    return NULL;
  }

  int family = (addr->family == net_addr_family_v6) ? AF_INET6 : AF_INET;
  int type = (proto == net_proto_udp) ? SOCK_DGRAM : SOCK_STREAM;

  int fd = socket(family, type, 0);
  if (fd < 0) {
    sys_atomic_dec(&listener->claimed);
    return NULL;
  }

  int reuse = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct sockaddr_storage sa;
  socklen_t sa_len;
  if (!_net_addr_to_sockaddr(addr, port, &sa, &sa_len) ||
      bind(fd, (struct sockaddr *)&sa, sa_len) != 0) {
    close(fd);
    sys_atomic_dec(&listener->claimed);
    return NULL;
  }

  if (proto == net_proto_tcp && listen(fd, 8) != 0) {
    close(fd);
    sys_atomic_dec(&listener->claimed);
    return NULL;
  }

  listener->proto = proto;
  listener->fd = fd;
  listener->callback = callback;
  listener->userdata = userdata;
  listener->wg = sys_waitgroup_init();
  if (listener->wg == NULL) {
    close(fd);
    sys_atomic_dec(&listener->claimed);
    return NULL;
  }

  sys_atomic_init(&listener->running, 1);
  if (!sys_waitgroup_add(listener->wg, 1)) {
    sys_waitgroup_deinit(listener->wg);
    close(fd);
    sys_atomic_dec(&listener->claimed);
    return NULL;
  }

  sys_thread_func_t thread_fn = (proto == net_proto_udp)
                                    ? _net_listener_udp_thread
                                    : _net_listener_tcp_thread;
  if (!sys_thread_create(thread_fn, listener)) {
    sys_waitgroup_done(listener->wg);
    sys_waitgroup_deinit(listener->wg);
    close(fd);
    sys_atomic_dec(&listener->claimed);
    return NULL;
  }

  return listener;
}

void net_listener_deinit(net_listener_t *listener) {
  if (listener == NULL) {
    return;
  }
  sys_atomic_set(&listener->running, 0);
  sys_waitgroup_wait(listener->wg);
  sys_waitgroup_deinit(listener->wg);
  close(listener->fd);
  sys_atomic_dec(&listener->claimed);
}
