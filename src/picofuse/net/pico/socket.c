#include "../../sys/iostream/iostream.h"
#include "private.h"
#include <pico/cyw43_arch.h>
#include <string.h>

#include "lwip/pbuf.h"
#include "lwip/udp.h"

// Fixed-capacity pool of connected streams - net_open()'s own connections
// (TCP and UDP alike) and every TCP connection a listener accepts (see
// listener.c's _net_listener_tcp_accept_cb()). Pico-only: unlike the
// POSIX backend, which heap-allocates one context per connection, this
// project's Pico-side pools are fixed-size, matching every other Pico
// pool (hid_device_t, net_listener_t, ...).
#ifndef NET_CONN_CAPACITY
#define NET_CONN_CAPACITY 4
#endif

// How long net_open()'s TCP path spin-polls waiting for tcp_connect()'s
// callback before giving up - lwIP's own SYN retransmit logic normally
// reports failure well before this via the err callback, this is only a
// defensive backstop.
#define NET_CONN_CONNECT_TIMEOUT_MS (30 * 1000)
#define NET_CONN_POLL_MS 2

// Max payload size for a single UDP datagram this backend will send or
// buffer on receive - large enough for a typical Ethernet-path MTU's
// worth of payload. A larger outgoing write is clamped to this; a larger
// incoming datagram is truncated the same way a too-small recv() buffer
// would truncate a real UDP socket's read.
#ifndef NET_CONN_UDP_DGRAM_MAX_SIZE
#define NET_CONN_UDP_DGRAM_MAX_SIZE 1500
#endif

// UDP has no flow control (no analogue of TCP's receive window/
// tcp_recved()), so an unread "connected" UDP stream (net_open()) could
// otherwise accumulate unboundedly. Cap it and silently drop further
// datagrams past this many not-yet-fully-read datagrams - the same
// best-effort trade-off the POSIX backend's own fixed-size queue makes.
#ifndef NET_CONN_UDP_QUEUE_CAPACITY
#define NET_CONN_UDP_QUEUE_CAPACITY 4
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef enum {
  _net_conn_tcp,
  _net_conn_udp,
} _net_conn_kind_t;

// Backs net_open()'s own connections (TCP and UDP) and each TCP
// connection a listener accepts (via _net_conn_wrap_tcp()).
//
// For TCP (a plain byte stream, no message boundaries to preserve),
// received data accumulates as a single pbuf chain in rx.tcp_rx,
// unconsumed by the app until sys_iostream_read() drains it -
// pbuf_free_header() (see _net_conn_ops_read()) both copies out and
// frees/advances the chain in one step. This doubles as lwIP's
// flow-control signal: tcp_recved() is only called for bytes actually
// drained by the app, so an unread stream throttles its sender via the
// TCP window rather than growing unbounded.
//
// For UDP, each arriving datagram keeps its own identity: rx.udp_rx.chain
// is still one pbuf_cat()-joined chain (for the same copy-free-advance
// trick), but rx.udp_rx.lengths is a parallel queue of each still-unread
// datagram's own length, so _net_conn_ops_read() can stop draining right
// at a datagram's end instead of continuing into the next one - see its
// own doc for why that matters.
typedef struct {
  sys_atomic_t claimed;
  _net_conn_kind_t kind;
  union {
    struct tcp_pcb *tcp;
    struct udp_pcb *udp;
  } pcb;
  // UDP only: net_open()'s own remote address/port, used both to target
  // udp_sendto() below and to filter _net_conn_udp_recv_cb()'s incoming
  // datagrams by hand - see net_open()'s own doc on why this isn't done
  // via udp_connect()'s built-in remote-address filtering instead.
  ip_addr_t udp_remote_ip;
  u16_t udp_remote_port;
  sys_iostream_t *stream;
  bool gone; // true once lwIP has already freed pcb (tcp_err() fired) -
             // never touch pcb again.
  bool connect_done; // TCP net_open() only: set by the connected/err
                     // callback to end the spin-wait below.
  err_t connect_err; // TCP net_open() only: result once connect_done.
  // sys_iostream_peek()'s read-then-undo contract - see
  // hw/posix/uart.c's own last_byte/pushback for the same trick. Note:
  // for a UDP stream, peeking the *last* byte of a datagram and then
  // reading again can still glue it to the next datagram's first byte -
  // an accepted, narrow limitation of retrofitting a 1-byte-undo peek
  // onto datagram semantics (see _net_conn_ops_read()'s own doc).
  int last_byte;
  int pushback;
  union {
    struct pbuf *tcp_rx;
    struct {
      struct pbuf *chain;
      size_t lengths[NET_CONN_UDP_QUEUE_CAPACITY]; // oldest-first queue
      size_t head, count;  // circular queue over lengths[]
      size_t front_offset; // bytes already consumed from lengths[head]
    } udp_rx;
  } rx;
} _net_conn_ctx_t;

static _net_conn_ctx_t _net_conn_pool[NET_CONN_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// ADDRESS CONVERSION
//
// ip_addr_t is a dual-stack tagged union here (LWIP_IPV6 is on - see
// lwipopts.h), so these go through lwIP's own ip_2_ip4()/ip_2_ip6()
// accessors plus IP_SET_TYPE_VAL() rather than treating out/ip as a bare
// ip4_addr_t. ip6_addr_t's own 16 bytes (a u32_t addr[4], see lwip/
// ip6_addr.h) are the same network-byte-order layout net_addr_t.addr.v6
// already uses, so - like the v4 case - this is a plain copy, no
// byte-swapping.

bool _net_addr_to_ipaddr(const net_addr_t *addr, ip_addr_t *out) {
  if (addr->family == net_addr_family_v4) {
    uint32_t raw;
    memcpy(&raw, addr->addr.v4, sizeof(raw));
    ip4_addr_set_u32(ip_2_ip4(out), raw);
    IP_SET_TYPE_VAL(*out, IPADDR_TYPE_V4);
    return true;
  }
  if (addr->family == net_addr_family_v6) {
    memcpy(ip_2_ip6(out)->addr, addr->addr.v6, sizeof(addr->addr.v6));
    IP_SET_TYPE_VAL(*out, IPADDR_TYPE_V6);
    return true;
  }
  return false;
}

void _net_ipaddr_to_addr(const ip_addr_t *ip, net_addr_t *addr) {
  if (IP_IS_V6_VAL(*ip)) {
    addr->family = net_addr_family_v6;
    memcpy(addr->addr.v6, ip_2_ip6(ip)->addr, sizeof(addr->addr.v6));
    return;
  }
  addr->family = net_addr_family_v4;
  uint32_t raw = ip4_addr_get_u32(ip_2_ip4(ip));
  memcpy(addr->addr.v4, &raw, sizeof(addr->addr.v4));
}

///////////////////////////////////////////////////////////////////////////////
// POOL

static _net_conn_ctx_t *_net_conn_alloc(void) {
  for (size_t i = 0; i < NET_CONN_CAPACITY; i++) {
    _net_conn_ctx_t *ctx = &_net_conn_pool[i];
    if (sys_atomic_inc(&ctx->claimed) == 1) {
      ctx->stream = NULL;
      ctx->gone = false;
      ctx->connect_done = false;
      ctx->connect_err = ERR_OK;
      ctx->last_byte = -1;
      ctx->pushback = -1;
      memset(&ctx->rx, 0, sizeof(ctx->rx));
      return ctx;
    }
    sys_atomic_dec(&ctx->claimed);
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// CONNECTED STREAM (net_open(), and each TCP connection listener.c accepts)

static err_t _net_conn_tcp_recv_cb(void *arg, struct tcp_pcb *tpcb,
                                   struct pbuf *p, err_t err) {
  (void)tpcb;
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)arg;
  if (err != ERR_OK) {
    if (p != NULL) {
      pbuf_free(p);
    }
    return ERR_OK;
  }
  if (p == NULL) {
    // Remote FIN - nothing more will ever arrive; whatever is already
    // buffered in ctx->rx.tcp_rx remains readable. Notify once so the
    // app can notice via its own sys_iostream_read() returning 0 once that
    // buffered data (if any) is drained - the standard "readable, but
    // read() returns 0" idiom for EOF. Safe to call sys_iostream_close()
    // synchronously from within this callback in response - unlike the
    // POSIX backend there's no background thread/waitgroup to deadlock
    // against here (see private.h's own threading-model doc), and
    // calling tcp_close() from inside tcp_recv()'s own callback on a
    // NULL pbuf is the documented/canonical raw-API pattern (see e.g.
    // lwip/contrib/apps/tcpecho_raw/tcpecho_raw.c's own tcpecho_raw_recv()).
    sys_iostream_callback_t callback = ctx->stream->backend.net.callback;
    void *userdata = ctx->stream->backend.net.userdata;
    if (callback != NULL) {
      callback(ctx->stream, sys_iostream_event_read, userdata);
    }
    return ERR_OK;
  }
  if (ctx->rx.tcp_rx == NULL) {
    ctx->rx.tcp_rx = p;
  } else {
    pbuf_cat(ctx->rx.tcp_rx, p);
  }
  sys_iostream_callback_t callback = ctx->stream->backend.net.callback;
  void *userdata = ctx->stream->backend.net.userdata;
  if (callback != NULL) {
    callback(ctx->stream, sys_iostream_event_read, userdata);
  }
  return ERR_OK;
}

static void _net_conn_tcp_err_cb(void *arg, err_t err) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)arg;
  // lwIP has already freed tpcb by the time this fires - never touch it
  // again (see tcp_err()'s own doc).
  ctx->gone = true;
  if (!ctx->connect_done) {
    ctx->connect_err = (err != ERR_OK) ? err : ERR_ABRT;
    ctx->connect_done = true;
  }
}

static err_t _net_conn_tcp_connected_cb(void *arg, struct tcp_pcb *tpcb,
                                        err_t err) {
  (void)tpcb;
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)arg;
  ctx->connect_err = err;
  ctx->connect_done = true;
  return ERR_OK;
}

static void _net_conn_udp_recv_cb(void *arg, struct udp_pcb *upcb,
                                  struct pbuf *p, const ip_addr_t *addr,
                                  u16_t port) {
  (void)upcb;
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)arg;
  if (p == NULL) {
    return;
  }
  // Matches net_open()'s own promise that this stream only ever sees
  // datagrams from the address/port it was opened against - done here in
  // application code, on an otherwise-unconnected pcb, rather than via
  // udp_connect()'s own built-in remote-address filtering (see net_open()'s
  // own doc).
  if (!ip_addr_cmp(addr, &ctx->udp_remote_ip) || port != ctx->udp_remote_port) {
    pbuf_free(p);
    return;
  }
  if (ctx->rx.udp_rx.count >= NET_CONN_UDP_QUEUE_CAPACITY) {
    pbuf_free(p); // best-effort - see NET_CONN_UDP_QUEUE_CAPACITY's own doc
    return;
  }
  size_t slot = (ctx->rx.udp_rx.head + ctx->rx.udp_rx.count) %
               NET_CONN_UDP_QUEUE_CAPACITY;
  ctx->rx.udp_rx.lengths[slot] = p->tot_len;
  ctx->rx.udp_rx.count++;
  if (ctx->rx.udp_rx.chain == NULL) {
    ctx->rx.udp_rx.chain = p;
  } else {
    pbuf_cat(ctx->rx.udp_rx.chain, p);
  }
  sys_iostream_callback_t callback = ctx->stream->backend.net.callback;
  void *userdata = ctx->stream->backend.net.userdata;
  if (callback != NULL) {
    callback(ctx->stream, sys_iostream_event_read, userdata);
  }
}

// Reads buffered data. For TCP this drains rx.tcp_rx same as always - a
// plain byte stream, no boundaries to preserve. For UDP, this only ever
// returns bytes from a single datagram per call: it drains
// rx.udp_rx.lengths[head] worth of bytes from the front of the chain and
// stops there (even if the caller's buffer has room left and another
// datagram is already queued behind it) - the guarantee this exists for
// is that one sys_iostream_read() can never glue two separate datagrams'
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

#if PICO_CYW43_ARCH_POLL
  // A caller blocking on a reply by spinning sys_iostream_read()+
  // sys_sleep_ms() in its own loop (net_ntp_read(), say) never otherwise
  // gives anything a chance to service the CYW43 driver's own RX queue
  // in between - net_open()'s own TCP-connect wait loop already has to
  // pump cyw43_arch_poll() itself for the exact same reason (see its own
  // comment). Real-hardware testing showed a genuine reply sitting
  // unclaimed in the driver for the caller's *entire* wait, only
  // surfacing (as an orphaned "not for us" drop, its own pcb long since
  // torn down) once something else eventually called cyw43_arch_poll()
  // again - cheap and meant to be called liberally, so just always do it
  // here rather than depend on every future blocking caller getting this
  // right on its own. Only meaningful under the poll architecture - under
  // threadsafe_background, driver/lwIP work already happens on its own via
  // interrupt, and cyw43_arch_poll() isn't there to call.
  cyw43_arch_poll();
#endif

  size_t total = 0;
  if (ctx->pushback >= 0) {
    buf[total++] = (char)(uint8_t)ctx->pushback;
    ctx->last_byte = ctx->pushback;
    ctx->pushback = -1;
  }

  if (ctx->kind == _net_conn_udp) {
    if (total < n && ctx->rx.udp_rx.count > 0) {
      size_t front_len = ctx->rx.udp_rx.lengths[ctx->rx.udp_rx.head];
      size_t avail = front_len - ctx->rx.udp_rx.front_offset;
      size_t remain = n - total;
      size_t want = (remain < avail) ? remain : avail;
      u16_t copy_want = (u16_t)((want > 0xFFFFu) ? 0xFFFFu : want);
      cyw43_arch_lwip_begin();
      size_t got =
          pbuf_copy_partial(ctx->rx.udp_rx.chain, buf + total, copy_want, 0);
      ctx->rx.udp_rx.chain = pbuf_free_header(ctx->rx.udp_rx.chain, (u16_t)got);
      cyw43_arch_lwip_end();
      ctx->rx.udp_rx.front_offset += got;
      if (got > 0) {
        ctx->last_byte = (uint8_t)buf[total + got - 1];
      }
      total += got;
      if (ctx->rx.udp_rx.front_offset >= front_len) {
        ctx->rx.udp_rx.head =
            (ctx->rx.udp_rx.head + 1) % NET_CONN_UDP_QUEUE_CAPACITY;
        ctx->rx.udp_rx.count--;
        ctx->rx.udp_rx.front_offset = 0;
      }
      // never span into a second datagram within one call, whether or
      // not the caller's buffer had room left
    }
    return total;
  }

  if (total < n && ctx->rx.tcp_rx != NULL) {
    size_t remain = n - total;
    u16_t want = (u16_t)((remain > 0xFFFFu) ? 0xFFFFu : remain);
    cyw43_arch_lwip_begin();
    size_t got = pbuf_copy_partial(ctx->rx.tcp_rx, buf + total, want, 0);
    ctx->rx.tcp_rx = pbuf_free_header(ctx->rx.tcp_rx, (u16_t)got);
    if (!ctx->gone && got > 0) {
      tcp_recved(ctx->pcb.tcp, (u16_t)got);
    }
    cyw43_arch_lwip_end();
    if (got > 0) {
      ctx->last_byte = (uint8_t)buf[total + got - 1];
    }
    total += got;
  }

  return total;
}

static size_t _net_conn_ops_write(sys_iostream_t *s, const char *buf,
                                  size_t n) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  if (n == 0) {
    return 0;
  }

  // ctx->gone is set by _net_conn_tcp_err_cb() (an lwIP callback, so
  // already running with the lock held) - reading it needs the same lock
  // under pico_cyw43_arch_lwip_threadsafe_background, where that callback
  // can genuinely run concurrently with this function on a different
  // context, unlike under the poll architecture where nothing here is
  // truly concurrent. Check it inside the critical section rather than
  // before entering it.
  size_t written = 0;
  cyw43_arch_lwip_begin();
  if (ctx->gone) {
    cyw43_arch_lwip_end();
    return 0;
  }
  if (ctx->kind == _net_conn_tcp) {
    u16_t avail = tcp_sndbuf(ctx->pcb.tcp);
    u16_t want = (u16_t)((n < avail) ? n : avail);
    if (want > 0 &&
        tcp_write(ctx->pcb.tcp, buf, want, TCP_WRITE_FLAG_COPY) == ERR_OK) {
      tcp_output(ctx->pcb.tcp);
      written = want;
    }
  } else {
    size_t want =
        (n > NET_CONN_UDP_DGRAM_MAX_SIZE) ? NET_CONN_UDP_DGRAM_MAX_SIZE : n;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)want, PBUF_RAM);
    if (p != NULL) {
      pbuf_take(p, buf, (u16_t)want);
      if (udp_sendto(ctx->pcb.udp, p, &ctx->udp_remote_ip,
                     ctx->udp_remote_port) == ERR_OK) {
        written = want;
      }
      pbuf_free(p);
    }
  }
  cyw43_arch_lwip_end();
  return written;
}

// The only seek this stream supports is sys_iostream_peek()'s own "undo
// the single byte I just read" pattern - see hw/posix/uart.c's own
// _hw_uart_ops_seek().
static ptrdiff_t _net_conn_ops_seek(sys_iostream_t *s, ptrdiff_t offset,
                                    bool abs) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  if (abs || offset != -1 || ctx->last_byte < 0) {
    return -1;
  }
  ctx->pushback = ctx->last_byte;
  ctx->last_byte = -1;
  return 0;
}

static bool _net_conn_ops_set_callback(sys_iostream_t *s,
                                       sys_iostream_callback_t callback,
                                       void *userdata) {
  // No lock needed: unlike the POSIX backend, every recv callback that
  // reads these two fields runs on the same single thread/core as any
  // caller of this function - see private.h's own threading-model doc.
  s->backend.net.userdata = userdata;
  s->backend.net.callback = callback;
  return true;
}

static void _net_conn_ops_close(sys_iostream_t *s) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  cyw43_arch_lwip_begin();
  if (!ctx->gone) {
    if (ctx->kind == _net_conn_tcp) {
      tcp_arg(ctx->pcb.tcp, NULL);
      tcp_recv(ctx->pcb.tcp, NULL);
      tcp_err(ctx->pcb.tcp, NULL);
      if (tcp_close(ctx->pcb.tcp) != ERR_OK) {
        tcp_abort(ctx->pcb.tcp); // simplification: no retry-via-tcp_poll()
                                 // like a full graceful close would do
      }
    } else {
      udp_recv(ctx->pcb.udp, NULL, NULL);
      udp_remove(ctx->pcb.udp);
    }
  }
  if (ctx->kind == _net_conn_tcp) {
    if (ctx->rx.tcp_rx != NULL) {
      pbuf_free(ctx->rx.tcp_rx);
      ctx->rx.tcp_rx = NULL;
    }
  } else if (ctx->rx.udp_rx.chain != NULL) {
    pbuf_free(ctx->rx.udp_rx.chain);
    ctx->rx.udp_rx.chain = NULL;
  }
  cyw43_arch_lwip_end();
  sys_atomic_dec(&ctx->claimed);
}

static const sys_iostream_ops_t _net_conn_ops = {
    .read = _net_conn_ops_read,
    .write = _net_conn_ops_write,
    .seek = _net_conn_ops_seek,
    .set_callback = _net_conn_ops_set_callback,
    .close = _net_conn_ops_close,
};

static sys_iostream_t *_net_conn_finish(_net_conn_ctx_t *ctx) {
  sys_iostream_t *stream = _sys_iostream_alloc(&_net_conn_ops);
  if (stream == NULL) {
    cyw43_arch_lwip_begin();
    if (ctx->kind == _net_conn_tcp) {
      tcp_abort(ctx->pcb.tcp);
    } else {
      udp_remove(ctx->pcb.udp);
    }
    cyw43_arch_lwip_end();
    sys_atomic_dec(&ctx->claimed);
    return NULL;
  }
  ctx->stream = stream;
  stream->backend.net.instance = ctx;
  stream->backend.net.callback = NULL;
  stream->backend.net.userdata = NULL;
  return stream;
}

sys_iostream_t *_net_conn_wrap_tcp(struct tcp_pcb *pcb) {
  _net_conn_ctx_t *ctx = _net_conn_alloc();
  if (ctx == NULL) {
    cyw43_arch_lwip_begin();
    tcp_abort(pcb);
    cyw43_arch_lwip_end();
    return NULL;
  }
  ctx->kind = _net_conn_tcp;
  ctx->pcb.tcp = pcb;
  ctx->connect_done = true; // already connected - accepted, not dialed
  ctx->connect_err = ERR_OK;

  cyw43_arch_lwip_begin();
  tcp_arg(pcb, ctx);
  tcp_recv(pcb, _net_conn_tcp_recv_cb);
  tcp_err(pcb, _net_conn_tcp_err_cb);
  cyw43_arch_lwip_end();

  return _net_conn_finish(ctx);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

sys_iostream_t *net_open(net_proto_t proto, const net_addr_t *addr,
                         uint16_t port) {
  if (addr == NULL || !cyw43_is_initialized(&cyw43_state)) {
    return NULL;
  }
  ip_addr_t ip;
  if (!_net_addr_to_ipaddr(addr, &ip)) {
    return NULL;
  }

  _net_conn_ctx_t *ctx = _net_conn_alloc();
  if (ctx == NULL) {
    return NULL;
  }

  if (proto == net_proto_udp) {
    cyw43_arch_lwip_begin();
    // Deliberately left unconnected (no udp_connect()) - udp_sendto()/
    // _net_conn_udp_recv_cb() below carry the remote address/port
    // instead, matching pico-examples' own NTP client (see its
    // ntp_request()/ntp_recv()) rather than relying on udp_connect()'s
    // own built-in remote-address filtering, which real-hardware testing
    // showed silently swallowing every reply on this platform's lwIP/
    // cyw43 combination for reasons that stayed unclear even after an
    // extensive source audit turned up nothing conclusive.
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    if (pcb != NULL) {
      udp_recv(pcb, _net_conn_udp_recv_cb, ctx);
    }
    cyw43_arch_lwip_end();
    if (pcb == NULL) {
      sys_atomic_dec(&ctx->claimed);
      return NULL;
    }
    ctx->kind = _net_conn_udp;
    ctx->pcb.udp = pcb;
    ctx->udp_remote_ip = ip;
    ctx->udp_remote_port = port;
    return _net_conn_finish(ctx);
  }

  // TCP
  cyw43_arch_lwip_begin();
  // IPADDR_TYPE_ANY, not IPADDR_TYPE_V4 - see net_open()'s own UDP path
  // above, which already used ANY (for an unrelated reason, before IPv6
  // existed here) and turns out to be exactly right for dual-stack: an
  // ANY pcb adapts to whichever family tcp_connect()'s own destination
  // address (ip, from _net_addr_to_ipaddr() above) actually is.
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (pcb != NULL) {
    tcp_arg(pcb, ctx);
    tcp_err(pcb, _net_conn_tcp_err_cb);
    if (tcp_connect(pcb, &ip, port, _net_conn_tcp_connected_cb) != ERR_OK) {
      tcp_abort(pcb);
      pcb = NULL;
    }
  }
  cyw43_arch_lwip_end();
  if (pcb == NULL) {
    sys_atomic_dec(&ctx->claimed);
    return NULL;
  }
  ctx->kind = _net_conn_tcp;
  ctx->pcb.tcp = pcb;

  // net_open()'s documented contract is to block until connected or
  // failed - since this backend has no threads, that means pumping the
  // poll loop ourselves (under the poll architecture only - see
  // _net_conn_ops_read()'s own comment) rather than waiting for the
  // caller's own hw_poll() to eventually get around to it.
  uint64_t start = sys_timestamp_ms();
  while (!ctx->connect_done &&
        sys_timestamp_ms() - start < NET_CONN_CONNECT_TIMEOUT_MS) {
#if PICO_CYW43_ARCH_POLL
    cyw43_arch_poll();
#endif
    sys_sleep_ms(NET_CONN_POLL_MS);
  }

  if (!ctx->connect_done || ctx->connect_err != ERR_OK) {
    // See _net_conn_ops_write()'s own comment on why ctx->gone has to be
    // read inside the critical section, not before it.
    cyw43_arch_lwip_begin();
    if (!ctx->gone) {
      tcp_abort(pcb);
    }
    cyw43_arch_lwip_end();
    sys_atomic_dec(&ctx->claimed);
    return NULL;
  }

  cyw43_arch_lwip_begin();
  tcp_recv(pcb, _net_conn_tcp_recv_cb);
  cyw43_arch_lwip_end();

  return _net_conn_finish(ctx);
}
