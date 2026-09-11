#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <stdio.h> // sscanf() - only needed to parse --server's dotted-quad
#include <string.h>

// Connects to examples/echo/server and echoes stdin to it - anything the
// server (or another connected client, via its broadcast) sends back is
// printed to stdout. POSIX only: takes a --server argument, which doesn't
// fit this project's Pico examples (no interactive command line there).
#ifndef ECHO_PORT
#define ECHO_PORT 7777
#endif

static sys_atomic_t g_shutdown;

// sys_env_signalhandler()'s callback may run in a constrained context
// (see its own doc) - just flag it and let main()'s loop do the real
// work of closing conn.
static void _on_signal(sys_env_signal_t signal) {
  (void)signal;
  sys_atomic_set(&g_shutdown, 1);
}

static void _on_recv(sys_iostream_t *stream, sys_iostream_event_t events,
                     void *userdata) {
  (void)userdata;
  if (!(events & sys_iostream_event_read)) {
    return;
  }
  char buf[256];
  size_t n = sys_iostream_read(stream, buf, sizeof(buf));
  if (n > 0) {
    sys_iostream_write(sys_stdout, buf, n);
  }
}

static void _on_stdin(sys_iostream_t *stream, sys_iostream_event_t events,
                      void *userdata) {
  sys_iostream_t *conn = (sys_iostream_t *)userdata;
  if (!(events & sys_iostream_event_read)) {
    return;
  }
  char buf[256];
  size_t n = sys_iostream_read(stream, buf, sizeof(buf));
  if (n > 0) {
    sys_iostream_write(conn, buf, n);
    // Local echo: raw mode disables the terminal's own echo (see
    // sys/posix/stdio.c's own doc), so without this nothing typed here
    // would ever be visible.
    sys_iostream_write(sys_stdout, buf, n);
  }
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv, 0, sys_stdio_none);

  sys_env_arg_flag_t flags[] = {
      {.long_name = "server",
       .short_name = NULL,
       .type = sys_env_arg_type_string,
       .value = ""},
      {0},
  };
  sys_env_arg_t *args = sys_env_arg_parse(flags);
  char server[64] = {0};
  if (args != NULL) {
    sys_env_arg_parse_string(args, "server", server, sizeof(server));
  }
  if (server[0] == '\0') {
    sys_puts("usage: echo_client --server X.X.X.X\n");
    sys_exit();
    return 1;
  }

  unsigned a, b, c, d;
  if (sscanf(server, "%u.%u.%u.%u", &a, &b, &c, &d) != 4 || a > 255 ||
      b > 255 || c > 255 || d > 255) {
    sys_printf("invalid --server address: %s\n", server);
    sys_exit();
    return 1;
  }
  net_addr_t addr =
      net_addr_v4((uint8_t)a, (uint8_t)b, (uint8_t)c, (uint8_t)d);

  sys_printf("connecting to %s:%u...\n", server, ECHO_PORT);
  sys_iostream_t *conn = net_open(net_proto_tcp, &addr, ECHO_PORT, 0);
  if (conn == NULL) {
    sys_printf("failed to connect to %s:%u\n", server, ECHO_PORT);
    sys_exit();
    return 1;
  }
  sys_puts("connected - type to send, Ctrl-C to quit\n");

  sys_iostream_set_callback(conn, _on_recv, NULL);
  if (!sys_iostream_set_callback(sys_stdin, _on_stdin, conn)) {
    sys_puts("standard input callbacks are unavailable\n");
  }

  sys_atomic_init(&g_shutdown, 0);
  sys_env_signalhandler(sys_env_signal_none, _on_signal);

  while (sys_atomic_get(&g_shutdown) == 0) {
    sys_sleep_ms(200);
  }

  sys_puts("\ndisconnecting...\n");
  sys_iostream_close(conn);
  sys_exit();
  return 0;
}
