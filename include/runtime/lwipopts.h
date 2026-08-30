#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// Minimal lwIP configuration. pico_lwip_nosys is only linked to satisfy the
// CYW43 driver's raw low-level API, which always calls into lwIP's pbuf
// implementation for its packet buffers — even when not using lwIP's
// TCP/IP stack (CYW43_LWIP=0). This project doesn't otherwise use lwIP.
#define NO_SYS      1
#define LWIP_SOCKET 0
#define LWIP_NETCONN 0

#endif
