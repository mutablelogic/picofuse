#include "../../sys/iostream/iostream.h"
#include "private.h"
#include <pico/cyw43_arch.h>

#include "lwip/pbuf.h"
#include "lwip/udp.h"

// Fixed-capacity pool of single-shot UDP-listener datagram streams - see
// _net_dgram_ctx_t's own doc. Independent of NET_CONN_CAPACITY (socket.c),
// since a datagram stream never holds a live pcb of its own.
#ifndef NET_DGRAM_CAPACITY
#define NET_DGRAM_CAPACITY 4
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct net_listener_t {
  sys_atomic_t claimed;
  net_proto_t proto;
  union {
    struct tcp_pcb *tcp;
    struct udp_pcb *udp;
  } pcb;
  net_accept_callback_t callback;
  void *userdata;
};

static net_listener_t _net_listener_pool[NET_LISTENER_CAPACITY];

// Backs each single datagram net_listener_init(net_proto_udp, ...) hands
// to its callback - single-shot: one read (the datagram payload), one
// optional write (a reply, sent via the LISTENER's own pcb back to
// `remote`/`remote_port` - a datagram stream never owns a pcb itself),
// then closed. Never touched again by any lwIP callback once created, so
// (unlike _net_conn_ctx_t) it needs no "gone" bookkeeping.
typedef struct {
  sys_atomic_t claimed;
  struct udp_pcb *pcb; // borrowed from the listener - never removed here
  ip_addr_t remote_ip;
  u16_t remote_port;
  int last_byte; // see socket.c's _net_conn_ctx_t on why
  int pushback;
  struct pbuf *payload;
} _net_dgram_ctx_t;

static _net_dgram_ctx_t _net_dgram_pool[NET_DGRAM_CAPACITY];

///////////////////////////////////////////////////////////////////////////////
// POOLS

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

static _net_dgram_ctx_t *_net_dgram_alloc(void) {
  for (size_t i = 0; i < NET_DGRAM_CAPACITY; i++) {
    _net_dgram_ctx_t *ctx = &_net_dgram_pool[i];
    if (sys_atomic_inc(&ctx->claimed) == 1) {
      ctx->last_byte = -1;
      ctx->pushback = -1;
      ctx->payload = NULL;
      return ctx;
    }
    sys_atomic_dec(&ctx->claimed);
  }
  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// UDP DATAGRAM STREAM (single-shot: one read, one optional write, close)

static size_t _net_dgram_ops_read(sys_iostream_t *s, char *buf, size_t n) {
  _net_dgram_ctx_t *ctx = (_net_dgram_ctx_t *)s->backend.net.instance;
  if (n == 0) {
    return 0;
  }

  size_t total = 0;
  if (ctx->pushback >= 0) {
    buf[total++] = (char)(uint8_t)ctx->pushback;
    ctx->last_byte = ctx->pushback;
    ctx->pushback = -1;
  }

  if (total < n && ctx->payload != NULL) {
    size_t remain = n - total;
    u16_t want = (u16_t)((remain > 0xFFFFu) ? 0xFFFFu : remain);
    cyw43_arch_lwip_begin();
    size_t got = pbuf_copy_partial(ctx->payload, buf + total, want, 0);
    ctx->payload = pbuf_free_header(ctx->payload, (u16_t)got);
    cyw43_arch_lwip_end();
    if (got > 0) {
      ctx->last_byte = (uint8_t)buf[total + got - 1];
    }
    total += got;
  }

  return total;
}

static size_t _net_dgram_ops_write(sys_iostream_t *s, const char *buf,
                                   size_t n) {
  _net_dgram_ctx_t *ctx = (_net_dgram_ctx_t *)s->backend.net.instance;
  if (n == 0) {
    return 0;
  }
  size_t want = (n > 0xFFFFu) ? 0xFFFFu : n;
  size_t written = 0;
  cyw43_arch_lwip_begin();
  struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)want, PBUF_RAM);
  if (p != NULL) {
    pbuf_take(p, buf, (u16_t)want);
    if (udp_sendto(ctx->pcb, p, &ctx->remote_ip, ctx->remote_port) == ERR_OK) {
      written = want;
    }
    pbuf_free(p);
  }
  cyw43_arch_lwip_end();
  return written;
}

// See socket.c's _net_conn_ops_seek() - same one-byte-undo contract.
static ptrdiff_t _net_dgram_ops_seek(sys_iostream_t *s, ptrdiff_t offset,
                                     bool abs) {
  _net_dgram_ctx_t *ctx = (_net_dgram_ctx_t *)s->backend.net.instance;
  if (abs || offset != -1 || ctx->last_byte < 0) {
    return -1;
  }
  ctx->pushback = ctx->last_byte;
  ctx->last_byte = -1;
  return 0;
}

static void _net_dgram_ops_close(sys_iostream_t *s) {
  _net_dgram_ctx_t *ctx = (_net_dgram_ctx_t *)s->backend.net.instance;
  if (ctx->payload != NULL) {
    cyw43_arch_lwip_begin();
    pbuf_free(ctx->payload);
    cyw43_arch_lwip_end();
    ctx->payload = NULL;
  }
  sys_atomic_dec(&ctx->claimed);
}

static const sys_iostream_ops_t _net_dgram_ops = {
    .read = _net_dgram_ops_read,
    .write = _net_dgram_ops_write,
    .seek = _net_dgram_ops_seek,
    .set_callback = NULL, // single-shot - readiness callbacks make no sense
    .close = _net_dgram_ops_close,
};

///////////////////////////////////////////////////////////////////////////////
// CALLBACKS

static err_t _net_listener_tcp_accept_cb(void *arg, struct tcp_pcb *newpcb,
                                         err_t err) {
  net_listener_t *listener = (net_listener_t *)arg;
  if (err != ERR_OK || newpcb == NULL) {
    return ERR_VAL;
  }

  sys_iostream_t *conn = _net_conn_wrap_tcp(newpcb);
  if (conn == NULL) {
    return ERR_ABRT; // _net_conn_wrap_tcp() already aborted newpcb
  }

  net_addr_t remote;
  _net_ipaddr_to_addr(&newpcb->remote_ip, &remote);
  listener->callback(listener, conn, &remote, newpcb->remote_port,
                     listener->userdata);
  return ERR_OK;
}

static void _net_listener_udp_recv_cb(void *arg, struct udp_pcb *upcb,
                                      struct pbuf *p, const ip_addr_t *addr,
                                      u16_t port) {
  net_listener_t *listener = (net_listener_t *)arg;
  if (p == NULL) {
    return;
  }

  _net_dgram_ctx_t *ctx = _net_dgram_alloc();
  if (ctx == NULL) {
    pbuf_free(p);
    return;
  }
  ctx->pcb = upcb;
  ip_addr_copy(ctx->remote_ip, *addr);
  ctx->remote_port = port;
  ctx->payload = p;

  sys_iostream_t *stream = _sys_iostream_alloc(&_net_dgram_ops);
  if (stream == NULL) {
    pbuf_free(p);
    sys_atomic_dec(&ctx->claimed);
    return;
  }
  stream->backend.net.instance = ctx;
  stream->backend.net.callback = NULL;
  stream->backend.net.userdata = NULL;

  net_addr_t remote;
  _net_ipaddr_to_addr(addr, &remote);
  listener->callback(listener, stream, &remote, port, listener->userdata);
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

net_listener_t *net_listener_init(net_proto_t proto, const net_addr_t *addr,
                                  uint16_t port, net_accept_callback_t callback,
                                  void *userdata) {
  if (addr == NULL || callback == NULL || !cyw43_is_initialized(&cyw43_state)) {
    return NULL;
  }
  ip_addr_t ip;
  if (!_net_addr_to_ipaddr(addr, &ip)) {
    return NULL;
  }

  net_listener_t *listener = _net_listener_alloc();
  if (listener == NULL) {
    return NULL;
  }
  listener->proto = proto;
  listener->callback = callback;
  listener->userdata = userdata;

  if (proto == net_proto_udp) {
    cyw43_arch_lwip_begin();
    // IPADDR_TYPE_ANY, not IPADDR_TYPE_V4 - see socket.c's net_open()'s
    // own TCP path for why: an ANY pcb adapts to whichever family
    // udp_bind()'s own address (ip, from _net_addr_to_ipaddr() above)
    // actually is, so a caller can bind to net_addr_v4_any()/a specific
    // v4 address or net_addr_v6_any()/a specific v6 address with no
    // family-specific branching needed here.
    struct udp_pcb *pcb = udp_new_ip_type(IPADDR_TYPE_ANY);
    err_t err = (pcb != NULL) ? udp_bind(pcb, &ip, port) : ERR_MEM;
    if (pcb != NULL && err == ERR_OK) {
      udp_recv(pcb, _net_listener_udp_recv_cb, listener);
    }
    cyw43_arch_lwip_end();
    if (pcb == NULL || err != ERR_OK) {
      if (pcb != NULL) {
        cyw43_arch_lwip_begin();
        udp_remove(pcb);
        cyw43_arch_lwip_end();
      }
      sys_atomic_dec(&listener->claimed);
      return NULL;
    }
    listener->pcb.udp = pcb;
    return listener;
  }

  // TCP
  cyw43_arch_lwip_begin();
  // See this function's own UDP path above for why ANY, not V4.
  struct tcp_pcb *pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
  if (pcb != NULL && tcp_bind(pcb, &ip, port) != ERR_OK) {
    tcp_close(pcb);
    pcb = NULL;
  }
  struct tcp_pcb *listen_pcb = (pcb != NULL) ? tcp_listen(pcb) : NULL;
  if (pcb != NULL && listen_pcb == NULL) {
    // tcp_listen() failing leaves the original pcb untouched - ours to
    // close (unlike success, which frees/replaces it - see tcp_listen()'s
    // own doc).
    tcp_close(pcb);
  }
  if (listen_pcb != NULL) {
    tcp_arg(listen_pcb, listener);
    tcp_accept(listen_pcb, _net_listener_tcp_accept_cb);
  }
  cyw43_arch_lwip_end();

  if (listen_pcb == NULL) {
    sys_atomic_dec(&listener->claimed);
    return NULL;
  }
  listener->pcb.tcp = listen_pcb;
  return listener;
}

void net_listener_deinit(net_listener_t *listener) {
  if (listener == NULL) {
    return;
  }
  cyw43_arch_lwip_begin();
  if (listener->proto == net_proto_udp) {
    udp_recv(listener->pcb.udp, NULL, NULL);
    udp_remove(listener->pcb.udp);
  } else {
    tcp_arg(listener->pcb.tcp, NULL);
    tcp_accept(listener->pcb.tcp, NULL);
    tcp_close(listener->pcb.tcp);
  }
  cyw43_arch_lwip_end();
  sys_atomic_dec(&listener->claimed);
}
