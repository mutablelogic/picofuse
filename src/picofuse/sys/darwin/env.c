#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <picofuse/sys.h>
#include <stdlib.h>

///////////////////////////////////////////////////////////////////////////////
// PUBLIC FUNCTIONS

/** @brief Get the system's serial number. */
const char *sys_env_serial(void) {
  static char serial[64] = {0};
  if (serial[0] != '\0') {
    return serial;
  }

  io_service_t expert = IOServiceGetMatchingService(
      kIOMainPortDefault, IOServiceMatching("IOPlatformExpertDevice"));
  if (expert) {
    CFStringRef ref = (CFStringRef)IORegistryEntryCreateCFProperty(
        expert, CFSTR("IOPlatformSerialNumber"), kCFAllocatorDefault, 0);
    IOObjectRelease(expert);
    if (ref) {
      CFStringGetCString(ref, serial, sizeof(serial), kCFStringEncodingUTF8);
      CFRelease(ref);
    }
  }

  return (serial[0] != '\0') ? serial : "unknown";
}

/** @brief Get the name of the current program. */
const char *sys_env_name(void) {
  const char *name = getprogname();
  return (name && *name) ? name : "unknown";
}
