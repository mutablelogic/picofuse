/**
 * @file wifi.h
 * @brief Wi-Fi management interface
 * @defgroup WiFi WiFi
 * @ingroup Hardware
 *
 * Wi-Fi network management interface.
 *
 * A handle operates in one of two mutually exclusive modes, chosen by
 * which init function created it and fixed for that handle's lifetime -
 * there's no way to switch an existing handle from one mode to the other,
 * only to hw_wifi_deinit() it and init a new one in the other mode:
 * - Station mode (hw_wifi_init_client(), hw_wifi_init_device()): the device
 *   discovers and joins someone else's network. hw_wifi_scan(),
 *   hw_wifi_connect() and hw_wifi_disconnect() are asynchronous and notify
 *   status updates through whatever callback is currently attached via
 *   hw_wifi_set_callback() - a handle starts with none attached, so init
 *   itself never delivers a notification.
 * - Access-point mode (hw_wifi_init_accesspoint()): the device broadcasts
 *   its own network for others to join. hw_wifi_scan(), hw_wifi_connect()
 *   and hw_wifi_disconnect() are unavailable in this mode - see their own
 *   documentation. A callback can still be attached via
 *   hw_wifi_set_callback(), but it will never fire: backends have no way
 *   to report individual stations joining or leaving (see
 *   hw_wifi_init_accesspoint()'s own doc), so there's nothing to notify.
 *   hw_wifi_deinit() stops broadcasting.
 *
 * Init and callback registration are deliberately separate calls
 * (hw_wifi_init_client()/_accesspoint()/_device(), then
 * hw_wifi_set_callback()) rather than the callback being an init
 * parameter: this lets one part of a program bring the radio up while a
 * different, unrelated part (for example, picofuse/hid's
 * hid_register_wifi(), which only observes) attaches to it, without
 * either one needing to be the one that called init.
 *
 * When connecting (station mode only), the attached callback will be
 * invoked with the current status of the connection attempt, including
 * any relevant network information. It will then be called occasionally
 * with updates on the connection status (for example, the signal
 * strength).
 *
 * When scanning (station mode only), the attached callback will be
 * invoked with the results of the scan, including information about any
 * discovered networks. The scan is completed when the callback is invoked
 * with a NULL network pointer.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////
// CONSTANTS

/**
 * @brief Maximum SSID length in bytes, excluding the NULL terminator.
 * @ingroup WiFi
 */
#ifndef HW_WIFI_SSID_MAX_LENGTH
#define HW_WIFI_SSID_MAX_LENGTH 32
#endif

///////////////////////////////////////////////////////////////////////////////
// TYPES

/**
 * @brief Authentication and cipher modes for Wi-Fi networks.
 * @ingroup WiFi
 *
 * Bitmask describing the advertised/required authentication/cipher modes.
 * Multiple bits may be set if a network supports more than one.
 */
typedef enum {
  hw_wifi_auth_open = (1 << 0),      ///< Open (no authentication)
  hw_wifi_auth_wep = (1 << 1),       ///< WEP (legacy)
  hw_wifi_auth_wpa_tkip = (1 << 2),  ///< WPA-PSK TKIP
  hw_wifi_auth_wpa_aes = (1 << 3),   ///< WPA-PSK CCMP/AES
  hw_wifi_auth_wpa2_tkip = (1 << 4), ///< WPA2-PSK TKIP
  hw_wifi_auth_wpa2_aes = (1 << 5),  ///< WPA2-PSK CCMP/AES
  hw_wifi_auth_wpa3_sae = (1 << 6),  ///< WPA3-SAE
  hw_wifi_auth_enterprise = (1 << 7) ///< 802.1X Enterprise (EAP)
} hw_wifi_auth_t;

/**
 * @brief Wi-Fi callback event flags.
 * @ingroup WiFi
 */
typedef enum {
  hw_wifi_event_scan = (1 << 0),         ///< Scan result available
  hw_wifi_event_joining = (1 << 1),      ///< Joining a network
  hw_wifi_event_connected = (1 << 2),    ///< Successfully connected
  hw_wifi_event_disconnected = (1 << 3), ///< Disconnected
  hw_wifi_event_badauth = (1 << 4),      ///< Bad authentication during
                                         ///< connection attempt
  hw_wifi_event_notfound = (1 << 5),     ///< Network not found
  hw_wifi_event_error = (1 << 6),        ///< Other error occurred
  hw_wifi_event_status = (1 << 7),       ///< Periodic status refresh while
                                         ///< already connected (updated
                                         ///< RSSI, channel, BSSID) - not a
                                         ///< new connection; see
                                         ///< hw_wifi_event_connected for
                                         ///< the one-time "just joined"
                                         ///< transition.
} hw_wifi_event_t;

/**
 * @brief Describes a discovered Wi-Fi network (scan result).
 * @ingroup WiFi
 *
 * This is a compact, platform-neutral representation of a single access
 * point reported during a scan.
 *
 * Notes:
 * - The SSID is a NULL-terminated string. The pointer value may reference a
 *   temporary buffer that is only valid for the duration of the callback;
 *   copy it if you need to retain it after the callback returns.
 * - RSSI is in dBm (negative values typical; higher is better).
 * - The auth field encodes authentication/cipher information; see
 *   hw_wifi_auth_t for details.
 */
typedef struct {
  char ssid[HW_WIFI_SSID_MAX_LENGTH +
            1];        ///< SSID (max 32 bytes) plus NULL termination
  uint8_t bssid[6];    ///< BSSID (MAC address) in network byte order
  hw_wifi_auth_t auth; ///< Authentication/cipher info (see hw_wifi_auth_t)
  uint8_t channel;     ///< Primary channel number
  int16_t rssi;        ///< Received signal strength (dBm)
} hw_wifi_network_t;

/**
 * @brief Opaque Wi-Fi handle.
 * @ingroup WiFi
 * @headerfile wifi.h hw/hw.h
 */
typedef struct hw_wifi_t hw_wifi_t;

/**
 * @brief Callback invoked for Wi-Fi operation notifications.
 * @ingroup WiFi
 * @param wifi The Wi-Fi handle associated with the operation.
 * @param event The event type (see hw_wifi_event_t).
 * @param network When event is hw_wifi_event_scan, this contains a pointer
 * to the current scan result, or NULL to indicate the scan operation has
 * completed.
 * @param userdata User-defined data pointer supplied to
 * hw_wifi_set_callback().
 *
 * This callback is used when connecting or disconnecting from a network,
 * and when scanning for networks. See hw_wifi_set_callback() to attach
 * one.
 */
typedef void (*hw_wifi_callback_t)(hw_wifi_t *wifi, hw_wifi_event_t event,
                                   const hw_wifi_network_t *network,
                                   void *userdata);

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

/** @name Lifecycle
 * @{ */

/**
 * @brief Initialize Wi-Fi as a client.
 * @ingroup WiFi
 * @param country_code Country code for the Wi-Fi region (e.g. "US", "EU").
 * If NULL, defaults to "XX" (worldwide).
 * @return Wi-Fi handle, or NULL when unsupported, on failure, or when
 * another handle is already live (see hw_wifi_t).
 *
 * The returned handle has no callback attached - operations proceed
 * normally, but nothing is notified until hw_wifi_set_callback() is
 * called.
 */
hw_wifi_t *hw_wifi_init_client(const char *country_code);

/**
 * @brief Initialize Wi-Fi as an access point.
 * @ingroup WiFi
 * @param country_code Country code for the Wi-Fi region (e.g. "US", "EU").
 * If NULL, defaults to "XX" (worldwide).
 * @param ssid SSID to broadcast. Must not be NULL, and must not exceed
 * @ref HW_WIFI_SSID_MAX_LENGTH bytes.
 * @param password NUL-terminated password string. May be NULL or empty
 * only when @p auth is hw_wifi_auth_open.
 * @param auth Authentication mode the access point requires. This selects
 * exactly one mode, unlike hw_wifi_network_t's own auth field, which
 * reports every mode a scanned network advertises support for.
 * @return Wi-Fi handle, or NULL when unsupported, @p ssid/@p auth are
 * invalid, or another handle is already live (see hw_wifi_t).
 *
 * Unlike hw_wifi_init_client()/hw_wifi_init_device(), this puts the radio
 * into access-point mode - other devices connect to it, rather than it
 * connecting to an existing network. Supported only where the backend's
 * radio can run in AP mode (the CYW43 chip on Pico W/2W boards); NULL
 * elsewhere.
 *
 * There's no callback parameter: unlike station mode, backends have no
 * way to report individual stations joining or leaving an access point
 * (confirmed on Pico - the CYW43 driver's own low-level per-station
 * association event handling for AP mode isn't wired up, and attempting
 * to read the AP's aggregate link status from the polling loop was found
 * to reliably deadlock the driver on real hardware). The access point
 * itself stays up silently from here until hw_wifi_deinit().
 */
hw_wifi_t *hw_wifi_init_accesspoint(const char *country_code,
                                    const char *ssid, const char *password,
                                    hw_wifi_auth_t auth);

/**
 * @brief Initialize Wi-Fi from a WPA supplicant device.
 * @ingroup WiFi
 * @param device Device identifier for the Wi-Fi interface, e.g.
 * `/var/run/wpa_supplicant/wlan0`.
 * @return Wi-Fi handle, or NULL when unsupported, on failure, or when
 * another handle is already live (see hw_wifi_t).
 *
 * This entry point is intended for platforms where Wi-Fi is managed by a
 * system service (wpa_supplicant) reachable over a control socket, rather
 * than a radio directly driven by this process - on Linux, the standard way
 * to manage Wi-Fi. Unsupported elsewhere.
 *
 * The returned handle has no callback attached - see
 * hw_wifi_init_client()'s own doc.
 *
 * @todo Not implemented yet on Linux - always returns NULL there (see
 * `src/picofuse/hw/linux/CMakeLists.txt`, which always falls back to
 * `hw/stub/wifi.c` rather than gating a real backend behind
 * `PICOFUSE_WIFI` the way `hw/pico/CMakeLists.txt` does). Needs a real
 * wpa_supplicant control-socket client under `picofuse/hw`.
 */
hw_wifi_t *hw_wifi_init_device(const char *device);

/**
 * @brief Attach or replace the callback notified of Wi-Fi status updates.
 * @ingroup WiFi
 * @param wifi Wi-Fi handle, from any of the hw_wifi_init_*() functions.
 * @param callback Callback to notify of connection/disconnection/scanning
 * status updates, or NULL to detach the current callback.
 * @param userdata User-defined data pointer forwarded to @p callback.
 *
 * Separate from init so that whichever part of a program brought the
 * radio up doesn't have to be the same part that observes it - see this
 * file's own top-level doc. Safe to call at any time, including while an
 * operation is in progress or on an access-point handle (where it has no
 * observable effect - see hw_wifi_init_accesspoint()'s own doc). A no-op
 * on an invalid handle.
 */
void hw_wifi_set_callback(hw_wifi_t *wifi, hw_wifi_callback_t callback,
                          void *userdata);

/**
 * @brief Deinitialize and release a Wi-Fi handle.
 * @ingroup WiFi
 * @param wifi Wi-Fi handle.
 *
 * Safe to call on NULL. Requests that any in-progress connection,
 * disconnection or scan stop first, but whether that request can actually
 * interrupt an operation already under way is backend-dependent - some
 * backends have no way to cancel a scan/connect once started (see
 * hw_wifi_disconnect()'s own doc), in which case a callback for the old
 * operation may still arrive after this call returns, referencing a
 * handle that's already been released.
 */
void hw_wifi_deinit(hw_wifi_t *wifi);

/** @} */

///////////////////////////////////////////////////////////////////////////////
// METHODS

/** @name Methods
 * @{ */

/**
 * @brief Begin an asynchronous scan for nearby Wi-Fi networks.
 * @ingroup WiFi
 * @param wifi Initialized Wi-Fi handle, from hw_wifi_init_client() or
 * hw_wifi_init_device() - not hw_wifi_init_accesspoint(), see below.
 * @retval true Scan was started.
 * @retval false Handle is invalid, is an access-point handle, or an
 * operation (connection, disconnection or scanning) is already in
 * progress.
 *
 * Starts a non-blocking scan. The handle's callback is invoked once per
 * result (network != NULL) and once more with network == NULL when the
 * scan completes. This function returns immediately.
 *
 * A handle from hw_wifi_init_accesspoint() always fails here - scanning
 * requires the radio to be in station mode.
 */
bool hw_wifi_scan(hw_wifi_t *wifi);

/**
 * @brief Begin an asynchronous connection to a Wi-Fi network.
 * @ingroup WiFi
 * @param wifi Initialized Wi-Fi handle, from hw_wifi_init_client() or
 * hw_wifi_init_device() - not hw_wifi_init_accesspoint(), see below.
 * @param network Target network to connect to (SSID, BSSID, etc).
 * @param password NUL-terminated password string. May be NULL or empty for
 * open networks.
 * @retval true Connection attempt was started.
 * @retval false Handle or @p network is invalid, @p wifi is an
 * access-point handle, or an operation (connection, disconnection or
 * scanning) is already in progress.
 *
 * Initiates a non-blocking connection attempt to the specified network
 * using the provided password. The handle's callback is invoked to report
 * connection progress and completion. This function returns immediately.
 *
 * The bssid field of @p network can optionally be set to the BSSID of the
 * target access point, if known.
 *
 * A handle from hw_wifi_init_accesspoint() always fails here - an access
 * point doesn't join other networks; other devices join it.
 */
bool hw_wifi_connect(hw_wifi_t *wifi, const hw_wifi_network_t *network,
                     const char *password);

/**
 * @brief Disconnect from a previously-connected Wi-Fi network.
 * @ingroup WiFi
 * @param wifi Initialized Wi-Fi handle, from hw_wifi_init_client() or
 * hw_wifi_init_device() - not hw_wifi_init_accesspoint(), see below.
 * @retval true Disconnect was initiated.
 * @retval false Handle is invalid, is an access-point handle, or not
 * currently connected, connecting or scanning.
 *
 * Initiates a disconnect from the current network, or requests that an
 * in-progress connection attempt or scan stop. This function returns
 * immediately, but whether an in-progress operation can actually be
 * interrupted is backend-dependent - some backends have no cancellation
 * mechanism for a scan/connect already under way, in which case it runs
 * to completion regardless and still delivers its own callback for
 * whatever it was doing when this was called.
 *
 * A handle from hw_wifi_init_accesspoint() always fails here - there is no
 * "current network" for an access point to leave. Use hw_wifi_deinit() to
 * stop broadcasting instead.
 */
bool hw_wifi_disconnect(hw_wifi_t *wifi);

/** @} */
