#include <picofuse/sys.h>

static void stdin_callback(sys_iostream_t *stream, sys_iostream_event_t events,
                           void *userdata) {
  (void)userdata;
  if (!(events & sys_iostream_event_read)) {
    return;
  }

  char buf[32];
  size_t n = sys_iostream_read(stream, buf, sizeof(buf));
  if (n > 0) {
    sys_iostream_write(sys_stdout, "echo ", 5);
    sys_iostream_write(sys_stdout, buf, n);
    sys_iostream_write(sys_stdout, "\n", 1);
  }
}

int main(int argc, char *argv[]) {
  sys_init(argc, argv, 0);
  sys_puts("Echoing standard input.\n");

  if (!sys_iostream_set_callback(sys_stdin, stdin_callback, NULL)) {
    sys_puts("Standard input callbacks are unavailable.\n");
    sys_exit();
    return 1;
  }

  while (true) {
    sys_sleep_ms(1000);
  }
}