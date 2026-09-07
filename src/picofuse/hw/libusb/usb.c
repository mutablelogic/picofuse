#include <picofuse/hw.h>
#include <picofuse/sys.h>

#include <libusb.h>
#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

///////////////////////////////////////////////////////////////////////////////
// TYPES

struct hw_usb_t {
  hw_usb_callback_t callback;
  void *userdata;
  libusb_context *context;
  libusb_hotplug_callback_handle hotplug_handle;
  pthread_t thread;
  bool hotplug_registered;
  bool thread_started;
  // Set by hw_usb_set_callback() on a NULL->non-NULL transition, cleared by
  // _hw_usb_event_thread() once it's replayed the currently-known device
  // list to whatever callback is attached at that point - see
  // hw_usb_set_callback()'s own doc ("once attached...").
  bool replay_requested;
  atomic_bool running;
  atomic_bool cleanup_in_thread;
  bool init;
};

///////////////////////////////////////////////////////////////////////////////
// GLOBALS

static struct hw_usb_t _hw_usb_instance = {0};

// Guards every field above except `running`/`cleanup_in_thread` (left as
// plain atomics - they're polled in the event thread's own tight loop, and
// don't need the same protection as the rest of the struct). Without this,
// hw_usb_init()/hw_usb_deinit() (main thread) can race the background
// hotplug event thread's own field access: the common cross-thread deinit
// path is already safe on its own (pthread_join() is itself a
// synchronization point), but a deinit triggered from *inside* the user's
// own hotplug callback - which runs on the event thread - takes a
// different path (see hw_usb_deinit()'s own doc) that resets state from
// _hw_usb_event_thread()'s own tail, with nothing else to serialize that
// against a concurrent hw_usb_init()/hw_usb_deinit() call from the main
// thread. Never held across a call to the user's own callback, or any
// libusb/pthread call that can block - only ever around the plain field
// reads/writes themselves.
static pthread_mutex_t _hw_usb_lock = PTHREAD_MUTEX_INITIALIZER;
#define _HW_USB_LOCK() pthread_mutex_lock(&_hw_usb_lock)
#define _HW_USB_UNLOCK() pthread_mutex_unlock(&_hw_usb_lock)

///////////////////////////////////////////////////////////////////////////////
// PRIVATE

// Caller must already hold _HW_USB_LOCK(). Deliberately doesn't require
// usb->callback != NULL - a handle with nothing attached yet (see
// hw_usb_set_callback()'s own doc on why init and callback attachment are
// separate calls) is still a valid one; callers that specifically need "is
// anyone listening" (_hw_usb_emit_event() et al.) check usb->callback for
// that themselves.
static bool _hw_usb_valid(const hw_usb_t *usb) {
  return usb != NULL && usb->init && usb->context != NULL;
}

static void _hw_usb_populate_strings(libusb_device_handle *handle,
                                     const struct libusb_device_descriptor *dd,
                                     hw_usb_device_t *device) {
  if (handle == NULL || dd == NULL || device == NULL) {
    return;
  }

  if (dd->iManufacturer != 0) {
    int n = libusb_get_string_descriptor_ascii(
        handle, dd->iManufacturer, (unsigned char *)device->manufacturer,
        HW_USB_STRING_MAX_LENGTH);
    if (n < 0) {
      device->manufacturer[0] = '\0';
    } else {
      device->manufacturer[n] = '\0';
    }
  }

  if (dd->iProduct != 0) {
    int n = libusb_get_string_descriptor_ascii(handle, dd->iProduct,
                                               (unsigned char *)device->product,
                                               HW_USB_STRING_MAX_LENGTH);
    if (n < 0) {
      device->product[0] = '\0';
    } else {
      device->product[n] = '\0';
    }
  }

  if (dd->iSerialNumber != 0) {
    int n = libusb_get_string_descriptor_ascii(handle, dd->iSerialNumber,
                                               (unsigned char *)device->serial,
                                               HW_USB_STRING_MAX_LENGTH);
    if (n < 0) {
      device->serial[0] = '\0';
    } else {
      device->serial[n] = '\0';
    }
  }
}

static bool _hw_usb_make_device(libusb_device *dev, hw_usb_device_t *out) {
  if (dev == NULL || out == NULL) {
    return false;
  }

  struct libusb_device_descriptor dd = {0};
  if (libusb_get_device_descriptor(dev, &dd) != 0) {
    return false;
  }

  memset(out, 0, sizeof(*out));
  out->vid = dd.idVendor;
  out->pid = dd.idProduct;
  out->device_class = dd.bDeviceClass;
  out->device_subclass = dd.bDeviceSubClass;
  out->device_protocol = dd.bDeviceProtocol;

  libusb_device_handle *handle = NULL;
  if (libusb_open(dev, &handle) == 0 && handle != NULL) {
    _hw_usb_populate_strings(handle, &dd, out);
    libusb_close(handle);
  }

  return true;
}

// Captures whatever's needed under the lock, then calls the user's
// callback (which may itself call hw_usb_deinit() - see _HW_USB_LOCK()'s
// own doc on why that must never happen while this lock is held) outside
// it.
static void _hw_usb_emit_event(hw_usb_t *usb, hw_usb_event_t event,
                               libusb_device *dev) {
  if (usb == NULL || dev == NULL) {
    return;
  }

  _HW_USB_LOCK();
  bool valid = _hw_usb_valid(usb);
  hw_usb_callback_t callback = valid ? usb->callback : NULL;
  void *userdata = valid ? usb->userdata : NULL;
  _HW_USB_UNLOCK();

  if (callback == NULL) {
    return;
  }

  hw_usb_device_t device = {0};
  if (_hw_usb_make_device(dev, &device)) {
    callback(usb, event, &device, userdata);
  }
}

static int LIBUSB_CALL _hw_usb_hotplug_cb(libusb_context *context,
                                          libusb_device *dev,
                                          libusb_hotplug_event event,
                                          void *userdata) {
  (void)context;

  hw_usb_t *usb = (hw_usb_t *)userdata;
  if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED) {
    _hw_usb_emit_event(usb, hw_usb_event_attached, dev);
  } else if (event == LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT) {
    _hw_usb_emit_event(usb, hw_usb_event_detached, dev);
  }

  return 0;
}

static bool _hw_usb_emit_attached_devices(hw_usb_t *usb) {
  _HW_USB_LOCK();
  libusb_context *context = usb->context;
  _HW_USB_UNLOCK();

  libusb_device **list = NULL;
  ssize_t count = libusb_get_device_list(context, &list);
  if (count < 0 || list == NULL) {
    return false;
  }

  for (ssize_t i = 0; i < count; i++) {
    if (list[i] != NULL) {
      _hw_usb_emit_event(usb, hw_usb_event_attached, list[i]);
    }
  }

  libusb_free_device_list(list, 1);

  // Signal that initial enumeration is complete.
  _HW_USB_LOCK();
  bool valid = _hw_usb_valid(usb);
  hw_usb_callback_t callback = valid ? usb->callback : NULL;
  void *userdata = valid ? usb->userdata : NULL;
  _HW_USB_UNLOCK();
  if (callback != NULL) {
    callback(usb, hw_usb_event_attached, NULL, userdata);
  }

  return true;
}

static void _hw_usb_cleanup(hw_usb_t *usb) {
  if (usb == NULL) {
    return;
  }

  _HW_USB_LOCK();
  bool hotplug_registered = usb->hotplug_registered;
  libusb_context *context = usb->context;
  libusb_hotplug_callback_handle hotplug_handle = usb->hotplug_handle;
  usb->hotplug_registered = false;
  usb->context = NULL;
  _HW_USB_UNLOCK();

  if (hotplug_registered && context != NULL) {
    libusb_hotplug_deregister_callback(context, hotplug_handle);
  }

  if (context != NULL) {
    libusb_exit(context);
  }
}

static void *_hw_usb_event_thread(void *arg) {
  hw_usb_t *usb = (hw_usb_t *)arg;
  if (usb == NULL) {
    return NULL;
  }

  // Deferred here from hw_usb_init() itself - firing callbacks
  // synchronously from there let a callback that reacts to the
  // "enumeration complete" marker by calling hw_usb_deinit() on the very
  // handle it was just given (a natural thing to do - e.g. "my device
  // wasn't in the initial list, give up") reentrantly tear down the same
  // libusb context hw_usb_init() was still using further down its own
  // call stack: a real, 100%-reproducible use-after-free, confirmed via
  // real hardware testing. Every callback invocation this backend makes -
  // this first pass included - now consistently happens off of whichever
  // thread called hw_usb_init(), matching hw_usb_init()'s own doc ("...or
  // defer the initial device callbacks until the next host poll cycle").
  (void)_hw_usb_emit_attached_devices(usb);

  // Keeps looping even without hotplug support (unlike before) - besides
  // libusb_handle_events_timeout()'s own event delivery when hotplug is
  // available, this loop is now also what serves replay_requested: a
  // NULL->non-NULL callback attach that happens after this thread's own
  // first pass above (the common case - hw_usb_init() never takes a
  // callback) would otherwise never see it, since _hw_usb_emit_event() and
  // _hw_usb_emit_attached_devices() only ever fire to whatever callback is
  // attached at the exact instant they run. Bounded latency either way:
  // ~200ms, from this loop's own polling/timeout interval.
  while (atomic_load_explicit(&usb->running, memory_order_acquire)) {
    _HW_USB_LOCK();
    libusb_context *context = usb->context;
    bool hotplug_registered = usb->hotplug_registered;
    _HW_USB_UNLOCK();

    if (hotplug_registered) {
      struct timeval timeout = {
          .tv_sec = 0,
          .tv_usec = 200000,
      };
      int rc = libusb_handle_events_timeout(context, &timeout);
      if (rc < 0 && rc != LIBUSB_ERROR_INTERRUPTED) {
        struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = 100000000,
        };
        nanosleep(&ts, NULL);
      }
    } else {
      struct timespec ts = {
          .tv_sec = 0,
          .tv_nsec = 200000000,
      };
      nanosleep(&ts, NULL);
    }

    _HW_USB_LOCK();
    bool replay = usb->replay_requested;
    usb->replay_requested = false;
    _HW_USB_UNLOCK();
    if (replay) {
      (void)_hw_usb_emit_attached_devices(usb);
    }
  }

  // Only reached here if hw_usb_deinit() was called from inside the user's
  // own callback (this same thread) - see hw_usb_deinit()'s own doc. The
  // cross-thread deinit path does this same cleanup itself, after
  // pthread_join() guarantees this thread has already exited.
  if (atomic_load_explicit(&usb->cleanup_in_thread, memory_order_acquire)) {
    _hw_usb_cleanup(usb);
    _HW_USB_LOCK();
    usb->thread_started = false;
    usb->init = false;
    usb->callback = NULL;
    usb->userdata = NULL;
    _HW_USB_UNLOCK();
  }

  return NULL;
}

///////////////////////////////////////////////////////////////////////////////
// LIFECYCLE

hw_usb_t *hw_usb_init(void) {
  sys_debugf("usb", "usb_init");
  hw_usb_deinit(&_hw_usb_instance);

  _HW_USB_LOCK();
  memset(&_hw_usb_instance, 0, sizeof(_hw_usb_instance));
  _HW_USB_UNLOCK();

  libusb_context *context = NULL;
  if (libusb_init(&context) != 0 || context == NULL) {
    hw_usb_deinit(&_hw_usb_instance);
    return NULL;
  }

  _HW_USB_LOCK();
  _hw_usb_instance.context = context;
  _hw_usb_instance.init = true;
  _HW_USB_UNLOCK();

  if (libusb_has_capability(LIBUSB_CAP_HAS_HOTPLUG)) {
    libusb_hotplug_callback_handle hotplug_handle;
    int rc = libusb_hotplug_register_callback(
        context,
        LIBUSB_HOTPLUG_EVENT_DEVICE_ARRIVED | LIBUSB_HOTPLUG_EVENT_DEVICE_LEFT,
        LIBUSB_HOTPLUG_NO_FLAGS, LIBUSB_HOTPLUG_MATCH_ANY,
        LIBUSB_HOTPLUG_MATCH_ANY, LIBUSB_HOTPLUG_MATCH_ANY, _hw_usb_hotplug_cb,
        &_hw_usb_instance, &hotplug_handle);
    if (rc == 0) {
      _HW_USB_LOCK();
      _hw_usb_instance.hotplug_registered = true;
      _hw_usb_instance.hotplug_handle = hotplug_handle;
      _HW_USB_UNLOCK();
    }
  }

  // Always start the event thread - even without hotplug support (or if
  // registering it failed just above), it still needs to run the initial
  // enumeration pass itself; see _hw_usb_event_thread()'s own doc on why
  // that can't happen here, synchronously, instead.
  atomic_store_explicit(&_hw_usb_instance.cleanup_in_thread, false,
                        memory_order_release);
  atomic_store_explicit(&_hw_usb_instance.running, true, memory_order_release);

  pthread_t thread;
  if (pthread_create(&thread, NULL, _hw_usb_event_thread,
                     &_hw_usb_instance) == 0) {
    _HW_USB_LOCK();
    _hw_usb_instance.thread = thread;
    _hw_usb_instance.thread_started = true;
    _HW_USB_UNLOCK();
  } else {
    atomic_store_explicit(&_hw_usb_instance.running, false,
                          memory_order_release);
    atomic_store_explicit(&_hw_usb_instance.cleanup_in_thread, false,
                          memory_order_release);
  }

  return &_hw_usb_instance;
}

void hw_usb_set_callback(hw_usb_t *usb, hw_usb_callback_t callback,
                         void *userdata) {
  if (usb == NULL) {
    return;
  }

  _HW_USB_LOCK();
  if (_hw_usb_valid(usb)) {
    if (usb->callback == NULL && callback != NULL) {
      usb->replay_requested = true;
    }
    usb->callback = callback;
    usb->userdata = userdata;
  }
  _HW_USB_UNLOCK();
}

/**
 * Two very different shutdown paths, depending on which thread calls this:
 *
 * - From any other thread (the common case): stop the event thread, then
 *   pthread_join() it - which is itself a synchronization point, so
 *   cleanup/resetting the struct afterwards is safe without needing the
 *   lock held across all of it.
 * - From inside the event thread itself (i.e. the user's own hotplug
 *   callback calls hw_usb_deinit() on the handle it was just given) -
 *   joining our own thread would deadlock, so this just flags
 *   cleanup_in_thread and returns; _hw_usb_event_thread()'s own tail does
 *   the actual cleanup once its loop notices `running` went false and
 *   exits.
 */
void hw_usb_deinit(hw_usb_t *usb) {
  if (usb == NULL) {
    return;
  }

  _HW_USB_LOCK();
  bool was_init = usb->init;
  bool thread_started = usb->thread_started;
  pthread_t thread = usb->thread;
  _HW_USB_UNLOCK();

  // hw_usb_init() unconditionally deinits the singleton instance first to
  // clear any stale prior state; skip the log in that (typically no-op)
  // case so it doesn't read as an init immediately undone.
  if (was_init) {
    sys_debugf("usb", "usb_deinit: usb=%p", (void *)usb);
  }

  atomic_store_explicit(&usb->running, false, memory_order_release);

  if (thread_started) {
    if (!pthread_equal(pthread_self(), thread)) {
      pthread_join(thread, NULL);
      _hw_usb_cleanup(usb);
      _HW_USB_LOCK();
      memset(usb, 0, sizeof(*usb));
      _HW_USB_UNLOCK();
      return;
    }

    atomic_store_explicit(&usb->cleanup_in_thread, true, memory_order_release);
    _HW_USB_LOCK();
    usb->init = false;
    _HW_USB_UNLOCK();
    return;
  }

  _hw_usb_cleanup(usb);
  _HW_USB_LOCK();
  memset(usb, 0, sizeof(*usb));
  _HW_USB_UNLOCK();
}
