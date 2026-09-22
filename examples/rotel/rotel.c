#include "rotel.h"
#include <picofuse/hw.h>
#include <string.h>

/////////////////////////////////////////////////////////////////////
// PRIVATE METHODS

// Writes n bytes in full, looping over sys_iostream_write() - which may
// write fewer bytes than asked - until they're all sent or a write
// reports 0 (nothing more can be written).
static bool _rotel_write_all(sys_iostream_t *stream, const char *buf,
                             size_t n) {
  size_t sent = 0;
  while (sent < n) {
    size_t got = sys_iostream_write(stream, buf + sent, n - sent);
    if (got == 0) {
      return false;
    }
    sent += got;
  }
  return true;
}

// Sends buf followed by a trailing '!' terminator - the protocol doc
// requires every command sent to the device (queries included - see
// _rotel_query() below) to end with '!', not the '|' this used to send.
static bool _rotel_command(rotel_t *rotel, const char *buf, size_t len) {
  if (rotel->uart == NULL) {
    return false;
  }
  return _rotel_write_all(rotel->uart, buf, len) &&
         _rotel_write_all(rotel->uart, "!", 1);
}

// Sends buf followed by a trailing '?!' terminator.
static bool _rotel_query(rotel_t *rotel, const char *buf, size_t len) {
  if (rotel->uart == NULL) {
    return false;
  }
  return _rotel_write_all(rotel->uart, buf, len) &&
         _rotel_write_all(rotel->uart, "?!", 2);
}

// Indexed by rotel_query_t.
static const char *const _rotel_query_str[rotel_query_count] = {
    [rotel_query_power] = "power",
    [rotel_query_source] = "source",
    [rotel_query_volume] = "volume",
    [rotel_query_mute] = "mute",
    [rotel_query_bypass] = "bypass",
    [rotel_query_bass] = "bass",
    [rotel_query_treble] = "treble",
    [rotel_query_balance] = "balance",
    [rotel_query_freq] = "freq",
    [rotel_query_speaker] = "speaker",
    [rotel_query_dimmer] = "dimmer",
    [rotel_query_pcusb] = "pcusb",
    [rotel_query_version] = "version",
    [rotel_query_pcusbversion] = "pc_version",
    [rotel_query_ip] = "ip",
    [rotel_query_mac] = "mac",
    [rotel_query_model] = "model",
    [rotel_query_discover] = "discover",
};

// Indexed by rotel_command_t - NULL for a command whose token instead
// depends on rotel_command()'s value argument (vol_set, dimmer_set).
static const char *const _rotel_command_str[rotel_command_count] = {
    [rotel_command_power_on] = "power_on",
    [rotel_command_power_off] = "power_off",
    [rotel_command_power_toggle] = "power_toggle",
    [rotel_command_vol_up] = "vol_up",
    [rotel_command_vol_down] = "vol_dwn",
    [rotel_command_vol_set] = NULL,
    [rotel_command_mute_toggle] = "mute",
    [rotel_command_mute_on] = "mute_on",
    [rotel_command_mute_off] = "mute_off",
    [rotel_command_source_cd] = "cd",
    [rotel_command_source_coax1] = "coax1",
    [rotel_command_source_coax2] = "coax2",
    [rotel_command_source_opt1] = "opt1",
    [rotel_command_source_opt2] = "opt2",
    [rotel_command_source_aux1] = "aux1",
    [rotel_command_source_aux2] = "aux2",
    [rotel_command_source_tuner] = "tuner",
    [rotel_command_source_phono] = "phono",
    [rotel_command_source_usb] = "usb",
    [rotel_command_source_bluetooth] = "bluetooth",
    [rotel_command_source_pcusb] = "pcusb",
    [rotel_command_play] = "play",
    [rotel_command_stop] = "stop",
    [rotel_command_pause] = "pause",
    [rotel_command_track_next] = "trkf",
    [rotel_command_track_prev] = "trkb",
    [rotel_command_bypass_on] = "bypass_on",
    [rotel_command_bypass_off] = "bypass_off",
    [rotel_command_bass_up] = "bass_up",
    [rotel_command_bass_down] = "bass_down",
    [rotel_command_bass_reset] = "bass_000",
    [rotel_command_treble_up] = "treble_up",
    [rotel_command_treble_down] = "treble_down",
    [rotel_command_treble_reset] = "treble_000",
    [rotel_command_balance_left] = "balance_l",
    [rotel_command_balance_right] = "balance_r",
    [rotel_command_balance_reset] = "balance_000",
    [rotel_command_speaker_a_toggle] = "speaker_a",
    [rotel_command_speaker_b_toggle] = "speaker_b",
    [rotel_command_speaker_a_on] = "speaker_a_on",
    [rotel_command_speaker_a_off] = "speaker_a_off",
    [rotel_command_speaker_b_on] = "speaker_b_on",
    [rotel_command_speaker_b_off] = "speaker_b_off",
    [rotel_command_dimmer_toggle] = "dimmer",
    [rotel_command_dimmer_set] = NULL,
};

// "<name>=<value>$" reply scanner state, driven one completed run at a
// time by _rotel_scan() below. The value can span more than one run
// (e.g. "opt1" is an alpha run "opt" followed by a digit run "1"), so
// _rotel_reply_value keeps accumulating until the '$' terminator arrives
// rather than expecting exactly one run.
typedef enum {
  _rotel_reply_name = 0, // expecting the field name (an alpha run)
  _rotel_reply_equals,   // expecting a single '=' token
  _rotel_reply_value,    // accumulating value runs until '$'
} _rotel_reply_state_t;

static void _rotel_reply_reset(rotel_t *rotel) {
  rotel->reply_state = (uint8_t)_rotel_reply_name;
  rotel->name_len = 0;
  rotel->value_len = 0;
}

static void _rotel_scan(rotel_t *rotel, sys_rune_class_t isa, const char *text,
                        size_t len) {
  switch ((_rotel_reply_state_t)rotel->reply_state) {
  case _rotel_reply_name:
    if (isa != sys_rune_alpha || len >= sizeof(rotel->name)) {
      _rotel_reply_reset(rotel);
      return;
    }
    memcpy(rotel->name, text, len);
    rotel->name_len = len;
    rotel->name[len] = '\0';
    rotel->reply_state = (uint8_t)_rotel_reply_equals;
    return;

  case _rotel_reply_equals:
    if (isa != sys_rune_symbol || len != 1 || text[0] != '=') {
      _rotel_reply_reset(rotel);
      return;
    }
    rotel->value_len = 0;
    rotel->reply_state = (uint8_t)_rotel_reply_value;
    return;

  case _rotel_reply_value:
    if (isa == sys_rune_symbol && len == 1 && text[0] == '$') {
      rotel->value[rotel->value_len] = '\0';
      if (rotel->callback != NULL) {
        rotel->callback(rotel, rotel->name, rotel->value);
      }
      _rotel_reply_reset(rotel);
      return;
    }
    if (rotel->value_len + len >= sizeof(rotel->value)) {
      _rotel_reply_reset(rotel);
      return;
    }
    memcpy(rotel->value + rotel->value_len, text, len);
    rotel->value_len += len;
    return;
  }
}

static void _rotel_flush(rotel_t *rotel) {
  if (!rotel->open) {
    return;
  }
  rotel->buf[rotel->len] = '\0';
  _rotel_scan(rotel, rotel->isa, rotel->buf, rotel->len);
  rotel->open = false;
  rotel->len = 0;
}

/////////////////////////////////////////////////////////////////////
// LIFECYCLE

sys_iostream_t *rotel_init_device(rotel_t *rotel, const char *device) {
  sys_iostream_t *uart = hw_uart_init_device(device, ROTEL_BAUD, NULL);
  memset(rotel, 0, sizeof(*rotel));
  rotel->uart = uart;
  return uart;
}

void rotel_deinit(rotel_t *rotel) {
  _rotel_flush(rotel);
  if (rotel->uart != NULL) {
    sys_iostream_close(rotel->uart);
  }
  memset(rotel, 0, sizeof(*rotel));
}

void rotel_set_callback(rotel_t *rotel, rotel_callback_t callback,
                        void *userdata) {
  rotel->callback = callback;
  rotel->userdata = userdata;
}

/////////////////////////////////////////////////////////////////////
// METHODS

void rotel_receive(rotel_t *rotel) {
  char raw[ROTEL_BUF_CAPACITY];
  size_t got = sys_iostream_read(rotel->uart, raw, sizeof(raw) - 1);
  if (got == 0) {
    return;
  }
  raw[got] = '\0';

  const char *p = raw;
  for (;;) {
    rune_t r;
    const char *next = sys_rune_next(p, &r);
    if (next == NULL) {
      break;
    }
    size_t width = (size_t)(next - p);
    sys_rune_class_t isa = sys_rune_isa(r);

    if (rotel->open && isa == rotel->isa &&
        rotel->len + width < sizeof(rotel->buf)) {
      memcpy(rotel->buf + rotel->len, p, width);
      rotel->len += width;
    } else {
      _rotel_flush(rotel);
      memcpy(rotel->buf, p, width);
      rotel->len = width;
      rotel->isa = isa;
      rotel->open = true;
    }

    // '$' always completes a reply on its own - flush it immediately
    // rather than waiting for a rune of a different class to arrive and
    // trigger the flush, which might not happen until the next
    // rotel_receive() call (or ever, if the device falls quiet).
    if (r == '$') {
      _rotel_flush(rotel);
    }

    p = next;
  }
}

bool rotel_query(rotel_t *rotel, rotel_query_t query) {
  if (query < 0 || query >= rotel_query_count) {
    return false;
  }
  const char *str = _rotel_query_str[query];
  return _rotel_query(rotel, str, strlen(str));
}

bool rotel_command(rotel_t *rotel, rotel_command_t cmd, uint8_t value) {
  if (cmd < 0 || cmd >= rotel_command_count) {
    return false;
  }

  if (cmd == rotel_command_vol_set) {
    if (value < 1 || value > 96) {
      return false;
    }
    char buf[8];
    size_t n = sys_sprintf(buf, sizeof(buf), "vol_%02u", value);
    return _rotel_command(rotel, buf, n);
  }

  if (cmd == rotel_command_dimmer_set) {
    if (value > 6) {
      return false;
    }
    char buf[8];
    size_t n = sys_sprintf(buf, sizeof(buf), "dimmer_%u", value);
    return _rotel_command(rotel, buf, n);
  }

  const char *str = _rotel_command_str[cmd];
  if (str == NULL) {
    return false;
  }
  return _rotel_command(rotel, str, strlen(str));
}

rotel_command_t rotel_command_parse(const char *name, size_t len) {
  for (rotel_command_t cmd = 0; cmd < rotel_command_count; cmd++) {
    const char *str = _rotel_command_str[cmd];
    if (str != NULL && strlen(str) == len && memcmp(str, name, len) == 0) {
      return cmd;
    }
  }
  return rotel_command_count;
}
