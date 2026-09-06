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

// UDP has no flow control (no analogue of TCP's receive window/
// tcp_recved()), so an unread "connected" UDP stream (net_open()) could
// otherwise accumulate unboundedly. Cap it and silently drop further
// datagrams past this many buffered bytes - the same best-effort
// trade-off the POSIX backend's own fixed-size ring buffer makes.
#ifndef NET_CONN_UDP_MAX_BUFFERED
#define NET_CONN_UDP_MAX_BUFFERED 2048
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

typedef enum {
  _net_conn_tcp,
  _net_conn_udp,
} _net_conn_kind_t;

// Backs net_open()'s own connections (TCP and UDP) and each TCP
// connection a listener accepts (via _net_conn_wrap_tcp()). Received data
// (from tcp_recv()/udp_recv()) accumulates as a pbuf chain in `rx`,
// unconsumed by the app until sys_iostream_read() drains it -
// pbuf_free_header() (see _net_conn_ops_read()) both copies out and frees/
// advances the chain in one step. For TCP this doubles as this stream's
// own receive buffer AND lwIP's flow-control signal: tcp_recved() is only
// called for bytes actually drained by the app, so an unread stream
// throttles its sender via the TCP window rather than growing unbounded.
typedef struct {
  sys_atomic_t claimed;
  _net_conn_kind_t kind;
  union {
    struct tcp_pcb *tcp;
    struct udp_pcb *udp;
  } pcb;
  sys_iostream_t *stream;
  bool gone; // true once lwIP has already freed pcb (tcp_err() fired) -
             // never touch pcb again.
  bool connect_done; // TCP net_open() only: set by the connected/err
                     // callback to end the spin-wait below.
  err_t connect_err; // TCP net_open() only: result once connect_done.
  // sys_iostream_peek()'s read-then-undo contract - see
  // hw/posix/uart.c's own last_byte/pushback for the same trick.
  int last_byte;
  int pushback;
  struct pbuf *rx;
} _net_conn_ctx_t;

static _net_conn_ctx_t _net_conn_pool[NET_CONN_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// ADDRESS CONVERSION
//
// ip_addr_t is exactly ip4_addr_t on this project's build (LWIP_IPV6 is
// never enabled - see private.h's own doc), so this is a plain 32-bit
// copy, not the tagged-union dance a dual-stack lwIP build would need.

bool _net_addr_to_ipaddr(const net_addr_t *addr, ip_addr_t *out) {
  if (addr->family != net_addr_family_v4) {
    return false;
  }
  uint32_t raw;
  memcpy(&raw, addr->addr.v4, sizeof(raw));
  ip4_addr_set_u32(out, raw);
  return true;
}

void _net_ipaddr_to_addr(const ip_addr_t *ip, net_addr_t *addr) {
  addr->family = net_addr_family_v4;
  uint32_t raw = ip4_addr_get_u32(ip);
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
      ctx->rx = NULL;
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
    return ERR_OK; // remote FIN - nothing more will ever arrive; whatever
                   // is already buffered in ctx->rx remains readable
  }
  if (ctx->rx == NULL) {
    ctx->rx = p;
  } else {
    pbuf_cat(ctx->rx, p);
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
  (void)addr;
  (void)port;
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)arg;
  if (p == NULL) {
    return;
  }
  u16_t buffered = (ctx->rx != NULL) ? ctx->rx->tot_len : 0;
  if (buffered >= NET_CONN_UDP_MAX_BUFFERED) {
    pbuf_free(p); // best-effort - see NET_CONN_UDP_MAX_BUFFERED's own doc
    return;
  }
  if (ctx->rx == NULL) {
    ctx->rx = p;
  } else {
    pbuf_cat(ctx->rx, p);
  }
  sys_iostream_callback_t callback = ctx->stream->backend.net.callback;
  void *userdata = ctx->stream->backend.net.userdata;
  if (callback != NULL) {
    callback(ctx->stream, sys_iostream_event_read, userdata);
  }
}

static size_t _net_conn_ops_read(sys_iostream_t *s, char *buf, size_t n) {
  _net_conn_ctx_t *ctx = (_net_conn_ctx_t *)s->backend.net.instance;
  if (n == 0) {
    return 0;
  }

  size_t total = 0;
  if (ctx->pushback >= 0) {
    buf[total++] = (char)(uint8_t)ctx->pushback;
    ctx->last_byte = ctx->pushback;
    ctx->pushback = -1;
  }

  if (total < n && ctx->rx != NULL) {
    size_t remain = n - total;
    u16_t want = (u16_t)((remain > 0xFFFFu) ? 0xFFFFu : remain);
    cyw43_arch_lwip_begin();
    size_t got = pbuf_copy_partial(ctx->rx, buf + total, want, 0);
    ctx->rx = pbuf_free_header(ctx->rx, (u16_t)got);
    if (ctx->kind == _net_conn_tcp && !ctx->gone && got > 0) {
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
  if (ctx->gone || n == 0) {
    return 0;
  }

  size_t written = 0;
  cyw43_arch_lwip_begin();
  if (ctx->kind == _net_conn_tcp) {
    u16_t avail = tcp_sndbuf(ctx->pcb.tcp);
    u16_t want = (u16_t)((n < avail) ? n : avail);
    if (want > 0 &&
        tcp_write(ctx->pcb.tcp, buf, want, TCP_WRITE_FLAG_COPY) == ERR_OK) {
      tcp_output(ctx->pcb.tcp);
      written = want;
    }
  } else {
    size_t want = (n > NET_CONN_UDP_MAX_BUFFERED) ? NET_CONN_UDP_MAX_BUFFERED : n;
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)want, PBUF_RAM);
    if (p != NULL) {
      pbuf_take(p, buf, (u16_t)want);
      if (udp_send(ctx->pcb.udp, p) == ERR_OK) {
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
  if (ctx->rx != NULL) {
    pbuf_free(ctx->rx);
    ctx->rx = NULL;
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
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_V4);
    err_t err = (pcb != NULL) ? udp_connect(pcb, &ip, port) : ERR_MEM;
    if (pcb != NULL && err == ERR_OK) {
      udp_recv(pcb, _net_conn_udp_recv_cb, ctx);
    }
    cyw43_arch_lwip_end();
    if (pcb == NULL || err != ERR_OK) {
      if (pcb != NULL) {
        cyw43_arch_lwip_begin();
        udp_remove(pcb);
        cyw43_arch_lwip_end();
      }
      sys_atomic_dec(&ctx->claimed);
      return NULL;
    }
    ctx->kind = _net_conn_udp;
    ctx->pcb.udp = pcb;
    return _net_conn_finish(ctx);
  }

  // TCP
  cyw43_arch_lwip_begin();
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
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
  // poll loop ourselves rather than waiting for the caller's own hw_poll()
  // to eventually get around to it.
  uint64_t start = sys_timestamp_ms();
  while (!ctx->connect_done &&
        sys_timestamp_ms() - start < NET_CONN_CONNECT_TIMEOUT_MS) {
    cyw43_arch_poll();
    sys_sleep_ms(NET_CONN_POLL_MS);
  }

  if (!ctx->connect_done || ctx->connect_err != ERR_OK) {
    if (!ctx->gone) {
      cyw43_arch_lwip_begin();
      tcp_abort(pcb);
      cyw43_arch_lwip_end();
    }
    sys_atomic_dec(&ctx->claimed);
    return NULL;
  }

  cyw43_arch_lwip_begin();
  tcp_recv(pcb, _net_conn_tcp_recv_cb);
  cyw43_arch_lwip_end();

  return _net_conn_finish(ctx);
}
