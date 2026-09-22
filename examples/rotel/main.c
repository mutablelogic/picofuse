#include "rotel.h"
#include <picofuse/app.h>
#include <picofuse/net.h>
#include <picofuse/sys.h>
#include <string.h>

/////////////////////////////////////////////////////////////////////
// GLOBALS

#define DEVICE_PATH_CAPACITY 256
#define MQTT_COMMAND_TOPIC "rotel/command"
#define MQTT_POLL_INTERVAL_MS 100

sys_env_arg_flag_t args[] = {
    {
        .long_name = "help",
        .short_name = "h",
        .type = sys_env_arg_type_bool,
        .value = "false",
    },
    {
        .long_name = "device",
        .short_name = "D",
        .type = sys_env_arg_type_string,
    },
    {0},
};

// Application-level state: the CLI-configured device path, the rotel
// protocol parser (which owns the UART it opens), and the MQTT client
// (not yet initialized/connected - just embedded here for now) and the
// broker it'll connect to, set manually in main() below for now.
typedef struct {
  char device[DEVICE_PATH_CAPACITY];
  rotel_t rotel;
  net_mqtt_t *mqtt;
  net_addr_t broker;
} state_t;

/////////////////////////////////////////////////////////////////////
// FORWARD DECLARATIONS

static void _on_rotel_state(rotel_t *rotel, const char *name,
                            const char *value);
static void _on_mqtt_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                           void *userdata);

/////////////////////////////////////////////////////////////////////
// LIFECYCLE

static void _on_start(app_t *app, void *userdata) {
  state_t *state = (state_t *)userdata;

  // Parse command-line arguments
  sys_env_arg_t *parsed_args = sys_env_arg_parse(args);
  if (parsed_args == NULL) {
    sys_printf("Failed to parse command-line arguments\n");
    app_shutdown(1);
  }

  // If help option is specified, print usage and exit
  bool help = false;
  sys_env_arg_parse_bool(parsed_args, "help", &help);
  if (help) {
    sys_fprintf(sys_stdout, "Usage:\n");
    sys_env_arg_usage(args, sys_stdout);
    app_shutdown(-1);
    return;
  }

  // Get the device argument
  if (sys_env_arg_parse_string(parsed_args, "device", state->device,
                               DEVICE_PATH_CAPACITY) == false) {
    sys_printf("Device argument is required\n");
    app_shutdown(1);
    return;
  }

  // Open the device as a UART interface
  sys_iostream_t *uart = rotel_init_device(&state->rotel, state->device);
  if (uart == NULL) {
    sys_printf("Failed to open UART device: %s\n", state->device);
    app_shutdown(2);
    return;
  }
  rotel_set_callback(&state->rotel, _on_rotel_state, state);

  // Attach the stream to HID for reading input from the UART device
  hid_t *hid = app_hid(app);
  if (hid == NULL) {
    sys_printf("Failed to get HID instance\n");
    rotel_deinit(&state->rotel);
    app_shutdown(3);
    return;
  } else if (hid_register_iostream(hid, uart, state) == false) {
    sys_printf("Failed to register UART device with HID\n");
    rotel_deinit(&state->rotel);
    app_shutdown(4);
    return;
  }

  // Drive net_poll() periodically - nothing else calls it, and without it
  // no MQTT traffic (connect aside, which is synchronous) actually gets
  // sent or received: subscribe, publish, and incoming messages are all
  // completed from inside net_poll().
  if (hid_register_timer(hid, 0, MQTT_POLL_INTERVAL_MS, true, state) == NULL) {
    sys_printf("Failed to register MQTT poll timer\n");
  }

  // Connect to the MQTT broker - best-effort: a failure here is logged
  // but doesn't stop the app, since rotel control itself doesn't depend
  // on it.
  state->mqtt = net_mqtt_init(&state->broker, NET_MQTT_PORT, 5000, NULL);
  if (state->mqtt == NULL) {
    sys_printf("Failed to initialize MQTT client\n");
  } else if (net_mqtt_connect(state->mqtt) == false) {
    sys_printf("Failed to connect to MQTT broker\n");
  } else {
    // Callback must be registered before subscribing - nothing is
    // queued for a callback that isn't set yet.
    net_mqtt_set_callback(state->mqtt, _on_mqtt_event, state);
    if (net_mqtt_subscribe(state->mqtt, MQTT_COMMAND_TOPIC, net_mqtt_qos_0) ==
        0) {
      sys_printf("Failed to subscribe to MQTT topic: %s\n", MQTT_COMMAND_TOPIC);
    }
  }
}

static void _on_stop(app_t *app, void *userdata) {
  (void)app;
  state_t *state = (state_t *)userdata;

  sys_printf("CTRL+C pressed, stopping application\n");
  if (rotel_command(&state->rotel, rotel_command_power_off, 0) == false) {
    sys_printf("Failed to send power_off command\n");
  }
  rotel_deinit(&state->rotel);

  if (state->mqtt != NULL) {
    net_mqtt_disconnect(state->mqtt);
    net_mqtt_deinit(state->mqtt);
    state->mqtt = NULL;
  }

  app_shutdown(0);
}

/////////////////////////////////////////////////////////////////////
// CALLBACKS

static void _on_rotel_state(rotel_t *rotel, const char *name,
                            const char *value) {
  state_t *state = (state_t *)rotel->userdata;
  sys_printf("%s=%s\n", name, value);

  if (state->mqtt == NULL) {
    return;
  }

  char topic[64];
  size_t n = sys_sprintf(topic, sizeof(topic), "rotel/query/%s", name);
  if (n >= sizeof(topic)) {
    return; // name too long to fit the topic buffer - drop it
  }

  if (net_mqtt_publish(state->mqtt, topic, value, strlen(value), net_mqtt_qos_0,
                       false) == 0) {
    sys_printf("Failed to publish to MQTT topic: %s\n", topic);
    return;
  } else {
    sys_printf("%s => %s\n", topic, value);
  }

  // net_mqtt_publish() only stages the message - net_poll() does the
  // actual send, and would otherwise only run on the next timer tick.
  // Several rotel replies can arrive within a single rotel_receive()
  // call (e.g. a volume ramp), and net_mqtt_publish() blocks waiting for
  // a free slot if a previous staged message hasn't been sent yet -
  // draining it here, rather than waiting for the timer, is what keeps
  // that wait from ever triggering: nothing else could run net_poll() to
  // unblock it, since this whole app is single-threaded.
  net_poll();
}

static void _on_mqtt_event(net_mqtt_t *mqtt, const net_mqtt_event_t *event,
                           void *userdata) {
  (void)mqtt;
  state_t *state = (state_t *)userdata;

  if (event->type != net_mqtt_event_received) {
    return;
  }

  const net_mqtt_received_t *msg = &event->data.received;
  rotel_command_t cmd =
      rotel_command_parse((const char *)msg->payload, msg->payload_len);
  if (cmd == rotel_command_count) {
    sys_printf("Unknown MQTT command: %.*s\n", (int)msg->payload_len,
               (const char *)msg->payload);
    return;
  }
  if (rotel_command(&state->rotel, cmd, 0) == false) {
    sys_printf("Failed to send MQTT-triggered command\n");
  } else {
    sys_printf("%s\n", msg->payload);
  }
}

static void _on_event(app_t *app, sys_event_t event, void *userdata) {
  state_t *state = (state_t *)userdata;
  hid_event_t *hid_event = (hid_event_t *)event;
  if (hid_event == NULL) {
    return;
  }

  switch (hid_event->type) {
  case hid_event_type_signal:
    _on_stop(app, userdata);
    break;
  case hid_event_type_iostream:
    rotel_receive(&state->rotel);
    break;
  case hid_event_type_timer:
    net_poll();
    break;
  default:
    sys_printf("Unhandled HID event type: %d\n", hid_event->type);
    break;
  }
  hid_event_free(hid_event);
}

/////////////////////////////////////////////////////////////////////
// MAIN

int main(int argc, char *argv[]) {
  // The application configuration
  state_t state = {0};

  // MQTT broker address - hardcoded for now, not yet read from a CLI
  // argument. mosquitto on 192.168.86.13:1883 (NET_MQTT_PORT's default).
  state.broker = net_addr_v4(192, 168, 86, 13);

  // Start the application with the specified configuration
  return app_main(argc, argv, app_flag_signal | app_flag_led, _on_start,
                  _on_event, &state);
}
