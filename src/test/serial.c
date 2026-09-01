#include "serial.h"

#include <picofuse/sys.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

// Maps a numeric baud rate to the termios Bxxx constant. Linux's Bxxx are
// small enum values, not the numeric rate itself (unlike BSD/Darwin, where
// e.g. B115200 == 115200), so a real lookup is needed to be portable across
// both. Falls back to B115200 for anything not covered here.
static speed_t baud_to_speed(uint32_t baud) {
  switch (baud) {
  case 9600:
    return B9600;
  case 19200:
    return B19200;
  case 38400:
    return B38400;
  case 57600:
    return B57600;
  case 115200:
    return B115200;
  case 230400:
    return B230400;
  default:
    return B115200;
  }
}

int serial_open(const char *path, uint32_t baud) {
  if (path == NULL || path[0] == '\0') {
    return -1;
  }

  int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    return -1;
  }

  struct termios tty;
  if (tcgetattr(fd, &tty) != 0) {
    close(fd);
    return -1;
  }

  cfmakeraw(&tty);
  speed_t speed = baud_to_speed(baud);
  cfsetispeed(&tty, speed);
  cfsetospeed(&tty, speed);

  tty.c_cflag |= (CLOCAL | CREAD);
  tty.c_cflag &= ~CRTSCTS;

  // Reads are driven by poll()'s own timeout below, not VMIN/VTIME.
  tty.c_cc[VMIN] = 0;
  tty.c_cc[VTIME] = 0;

  if (tcsetattr(fd, TCSANOW, &tty) != 0) {
    close(fd);
    return -1;
  }

  // Discard anything already sitting in the receive buffer
  tcflush(fd, TCIFLUSH);

  return fd;
}

int serial_open_rtt(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }

  struct sockaddr_in address = {
      .sin_family = AF_INET,
      .sin_port = htons(port),
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

bool serial_find_unused_port(uint16_t *port) {
  if (port == NULL) {
    return false;
  }

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }

  struct sockaddr_in address = {
      .sin_family = AF_INET,
      .sin_port = 0,
      .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
  };
  socklen_t address_size = sizeof(address);
  bool found =
      bind(fd, (const struct sockaddr *)&address, sizeof(address)) == 0 &&
      getsockname(fd, (struct sockaddr *)&address, &address_size) == 0;
  if (found) {
    *port = ntohs(address.sin_port);
  }
  close(fd);
  return found;
}

void serial_close(int fd) {
  if (fd >= 0) {
    close(fd);
  }
}

bool serial_wait_for_marker(int fd, uint64_t deadline_ms,
                            const char *success_prefix,
                            const char *fail_prefix) {
  char line[512];
  size_t line_len = 0;

  for (;;) {
    uint64_t now_ms = sys_timestamp_ms();
    if (now_ms >= deadline_ms) {
      sys_puts("Error: timed out waiting for a \"[TEST] [EXIT] \" or "
               "\"[PANIC] \" line on the serial port\n");
      return false;
    }

    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    int rc = poll(&pfd, 1, (int)(deadline_ms - now_ms));
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return false;
    }
    if (rc == 0) {
      continue; // loop back around to re-check the deadline
    }

    char chunk[256];
    ssize_t n = read(fd, chunk, sizeof(chunk));
    if (n <= 0) {
      continue;
    }

    for (ssize_t i = 0; i < n; i++) {
      char ch = chunk[i];
      if (ch == '\n' || line_len >= sizeof(line) - 1) {
        line[line_len] = '\0';
        sys_printf("%s\n", line);

        if (line_len > 0 && success_prefix &&
            sys_string_hasprefix(line, success_prefix)) {
          return true;
        }
        if (line_len > 0 && fail_prefix &&
            sys_string_hasprefix(line, fail_prefix)) {
          return false;
        }
        line_len = 0;
        continue;
      }
      if (ch != '\r') {
        line[line_len++] = ch;
      }
    }
  }
}
