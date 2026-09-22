#pragma once
#include <picofuse/sys.h>

/////////////////////////////////////////////////////////////////////
// TYPES

#define ROTEL_BUF_CAPACITY 256
#define ROTEL_BAUD 115200

// Forward-declared (with a tag, rather than the usual anonymous-struct
// typedef) so rotel_callback_t below can name it as its first parameter
// before the struct itself - which stores a rotel_callback_t field - is
// defined.
typedef struct rotel rotel_t;

/**
 * @brief Called from rotel_receive() as each "<name>=<value>" reply
 * completes. rotel->userdata (set by rotel_set_callback()) is available
 * for the callback's own use.
 */
typedef void (*rotel_callback_t)(rotel_t *rotel, const char *name,
                                 const char *value);

/**
 * @brief Rotel RS232 protocol parser state.
 *
 * Owns the UART opened by rotel_init_device() - independent of
 * picofuse's app/hid layers, which just register the returned iostream
 * for readiness events and call rotel_receive()/rotel_deinit() in turn.
 */
struct rotel {
  sys_iostream_t *uart;         // opened by rotel_init_device(), closed by rotel_deinit()

  // Rune-class run accumulator, extended one rune at a time across
  // rotel_receive() calls - see rotel_receive()'s own doc.
  sys_rune_class_t isa;         // class of the run being accumulated
  char buf[ROTEL_BUF_CAPACITY]; // bytes of that run seen so far
  size_t len;                   // bytes in buf
  bool open;                    // whether a run is in progress

  // "<name>=<value>$" reply scanner, fed one completed run at a time -
  // see _rotel_scan() in rotel.c. reply_state is a _rotel_reply_state_t,
  // kept as a plain uint8_t here since that enum is private to rotel.c.
  uint8_t reply_state;
  char name[ROTEL_BUF_CAPACITY];
  size_t name_len;
  char value[ROTEL_BUF_CAPACITY];
  size_t value_len;

  // Set by rotel_set_callback() - callback is invoked with userdata
  // stashed here for its own use (see rotel_callback_t above).
  rotel_callback_t callback;
  void *userdata;
};

/**
 * @brief Rotel command identifiers - each maps to the literal command
 * token rotel_command() sends, e.g. rotel_command_power_on sends
 * "power_on". rotel_command_vol_set and rotel_command_dimmer_set instead
 * take their value from rotel_command()'s value argument.
 */
typedef enum {
  rotel_command_power_on,
  rotel_command_power_off,
  rotel_command_power_toggle,
  rotel_command_vol_up,
  rotel_command_vol_down,
  rotel_command_vol_set,
  rotel_command_mute_toggle,
  rotel_command_mute_on,
  rotel_command_mute_off,
  rotel_command_source_cd,
  rotel_command_source_coax1,
  rotel_command_source_coax2,
  rotel_command_source_opt1,
  rotel_command_source_opt2,
  rotel_command_source_aux1,
  rotel_command_source_aux2,
  rotel_command_source_tuner,
  rotel_command_source_phono,
  rotel_command_source_usb,
  rotel_command_source_bluetooth,
  rotel_command_source_pcusb,
  rotel_command_play,
  rotel_command_stop,
  rotel_command_pause,
  rotel_command_track_next,
  rotel_command_track_prev,
  rotel_command_bypass_on,
  rotel_command_bypass_off,
  rotel_command_bass_up,
  rotel_command_bass_down,
  rotel_command_bass_reset,
  rotel_command_treble_up,
  rotel_command_treble_down,
  rotel_command_treble_reset,
  rotel_command_balance_left,
  rotel_command_balance_right,
  rotel_command_balance_reset,
  rotel_command_speaker_a_toggle,
  rotel_command_speaker_b_toggle,
  rotel_command_speaker_a_on,
  rotel_command_speaker_a_off,
  rotel_command_speaker_b_on,
  rotel_command_speaker_b_off,
  rotel_command_dimmer_toggle,
  rotel_command_dimmer_set,
  rotel_command_count, // number of entries - not a real command
} rotel_command_t;

/**
 * @brief Rotel query identifiers - each maps to the literal field name
 * rotel_query() sends, followed by '?' and the trailing '!' terminator,
 * e.g. rotel_query_power sends "power?!".
 */
typedef enum {
  rotel_query_power,
  rotel_query_source,
  rotel_query_volume,
  rotel_query_mute,
  rotel_query_bypass,
  rotel_query_bass,
  rotel_query_treble,
  rotel_query_balance,
  rotel_query_freq,
  rotel_query_speaker,
  rotel_query_dimmer,
  rotel_query_pcusb,
  rotel_query_version,
  rotel_query_pcusbversion,
  rotel_query_ip,
  rotel_query_mac,
  rotel_query_model,
  rotel_query_discover,
  rotel_query_count, // number of entries - not a real query
} rotel_query_t;

/////////////////////////////////////////////////////////////////////
// LIFECYCLE

/**
 * @brief Initialize rotel parser state and open device as a UART.
 * @param rotel Parser state to initialize.
 * @param device Path to the serial device (e.g. "/dev/ttyUSB0").
 * @return The opened iostream (also retained in rotel->uart, for
 * rotel_receive() and rotel_deinit() to use), or NULL on failure - the
 * caller is expected to register it with HID for readiness events.
 */
sys_iostream_t *rotel_init_device(rotel_t *rotel, const char *device);

/** @brief Flush any in-progress run, close the UART, and release rotel
 * parser state. */
void rotel_deinit(rotel_t *rotel);

/**
 * @brief Set the callback invoked for each parsed reply, replacing the
 * default of printing "name=value" to the console.
 * @param rotel Parser state, previously initialized with
 * rotel_init_device() (which would otherwise clear a callback set
 * beforehand).
 * @param callback Called from rotel_receive() as each reply completes, or
 * NULL to fall back to the default console print.
 * @param userdata Opaque pointer stashed in rotel->userdata for
 * callback's own use.
 */
void rotel_set_callback(rotel_t *rotel, rotel_callback_t callback,
                        void *userdata);

/////////////////////////////////////////////////////////////////////
// METHODS

/**
 * @brief Read whatever's newly available on rotel's UART and feed it,
 * one rune at a time, into the run accumulator, which in turn feeds each
 * completed run into the "<name>=<value>$" reply scanner - invoking
 * rotel's callback (or printing "name=value" if none is set - see
 * rotel_set_callback()) once a reply completes. A reply that doesn't
 * match that shape (wrong token where '=' or '$' was expected, or a
 * value too long to fit) resets the scanner back to expecting a field
 * name.
 * @param rotel Parser state, previously initialized with
 * rotel_init_device().
 */
void rotel_receive(rotel_t *rotel);

/**
 * @brief Send a rotel_query_t query, asking the device to report that
 * field's current value.
 * @param rotel Parser state, previously initialized with
 * rotel_init_device().
 * @param query Field to query.
 * @return true if the query was sent in full, false for an out-of-range
 * query.
 */
bool rotel_query(rotel_t *rotel, rotel_query_t query);

/**
 * @brief Send a rotel_command_t command.
 * @param rotel Parser state, previously initialized with
 * rotel_init_device().
 * @param cmd Command to send.
 * @param value Only used by rotel_command_vol_set (01-96, sent as
 * "vol_nn") and rotel_command_dimmer_set (0-6, sent as "dimmer_n") -
 * ignored, and unrestricted, for every other command.
 * @return true if the command was sent in full, false for an
 * out-of-range cmd, or an out-of-range value for vol_set/dimmer_set.
 */
bool rotel_command(rotel_t *rotel, rotel_command_t cmd, uint8_t value);

/**
 * @brief Look up the rotel_command_t whose wire token exactly matches
 * name - the reverse of rotel_command()'s own lookup.
 * @param name Command name to match (e.g. "power_on") - not required to
 * be null-terminated.
 * @param len Number of bytes in name.
 * @return The matching command, or rotel_command_count if none matches -
 * this includes rotel_command_vol_set/rotel_command_dimmer_set, which
 * have no fixed string to match against.
 */
rotel_command_t rotel_command_parse(const char *name, size_t len);
