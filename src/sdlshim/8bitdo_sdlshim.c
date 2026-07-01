#include <dlfcn.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <pthread.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#define REAL_SDL_DEFAULT "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/libSDL2-2.0.0.8bitdo-real.dylib"
#define FAKE_HAPTIC ((void *)0x8bd02cUL)
#define SDL_HAPTIC_RUMBLE 0x0800

typedef struct { unsigned char data[16]; } SDL_JoystickGUID;

static void *real_sdl;
static IOHIDManagerRef rumble_manager;
static IOHIDDeviceRef rumble_device;
static pthread_mutex_t rumble_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t rumble_main_low;
static uint8_t rumble_main_high;
static uint8_t rumble_main_power;
static uint64_t rumble_main_deadline_ms;
static uint64_t rumble_main_min_deadline_ms;
static uint8_t rumble_trigger_left;
static uint8_t rumble_trigger_right;
static uint8_t rumble_trigger_power;
static uint64_t rumble_trigger_deadline_ms;
static int rumble_worker_started;

static void log_line(const char *fmt, ...) {
    FILE *f = fopen("/tmp/8bitdo-sdlshim.log", "a");
    if (!f) return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(f, "%lld.%03d ", (long long)tv.tv_sec, (int)(tv.tv_usec / 1000));
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static void *sym(const char *name) {
    if (!real_sdl) {
        const char *path = getenv("REAL_SDL2_DYLIB");
        if (!path || !*path) path = REAL_SDL_DEFAULT;
        real_sdl = dlopen(path, RTLD_LAZY | RTLD_LOCAL);
        if (!real_sdl) {
            fprintf(stderr, "[8bitdo-sdlshim] dlopen real SDL failed: %s\n", dlerror());
        }
    }
    return real_sdl ? dlsym(real_sdl, name) : NULL;
}

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    if (!number) return;
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static IOHIDDeviceRef open_rumble_device_locked(void) {
    if (rumble_device) return rumble_device;

    rumble_manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!rumble_manager) return NULL;

    CFMutableDictionaryRef match = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    if (!match) return NULL;
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2dc8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x301b);
    IOHIDManagerSetDeviceMatching(rumble_manager, match);
    CFRelease(match);

    if (IOHIDManagerOpen(rumble_manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        CFRelease(rumble_manager);
        rumble_manager = NULL;
        return NULL;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(rumble_manager);
    if (!devices || CFSetGetCount(devices) == 0) {
        if (devices) CFRelease(devices);
        IOHIDManagerClose(rumble_manager, kIOHIDOptionsTypeNone);
        CFRelease(rumble_manager);
        rumble_manager = NULL;
        return NULL;
    }

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    if (!values) {
        CFRelease(devices);
        return NULL;
    }
    CFSetGetValues(devices, values);
    for (CFIndex i = 0; i < count; i++) {
        IOHIDDeviceRef candidate = (IOHIDDeviceRef)values[i];
        CFTypeRef transport = IOHIDDeviceGetProperty(candidate, CFSTR(kIOHIDTransportKey));
        CFTypeRef max_output = IOHIDDeviceGetProperty(candidate, CFSTR(kIOHIDMaxOutputReportSizeKey));
        int output_size = 0;
        if (max_output && CFGetTypeID(max_output) == CFNumberGetTypeID()) {
            CFNumberGetValue((CFNumberRef)max_output, kCFNumberIntType, &output_size);
        }
        if (transport && CFGetTypeID(transport) == CFStringGetTypeID() &&
            CFStringCompare((CFStringRef)transport, CFSTR("Bluetooth Low Energy"), 0) == kCFCompareEqualTo &&
            output_size == 5) {
            rumble_device = candidate;
            break;
        }
    }
    if (!rumble_device) {
        rumble_device = (IOHIDDeviceRef)values[0];
        log_line("open fallback HID device; BLE MaxOutputReportSize=5 device not found");
    } else {
        log_line("open BLE HID rumble device");
    }
    CFRetain(rumble_device);
    free(values);
    CFRelease(devices);

    IOReturn open_result = IOHIDDeviceOpen(rumble_device, kIOHIDOptionsTypeNone);
    if (open_result != kIOReturnSuccess && open_result != kIOReturnExclusiveAccess) {
        CFRelease(rumble_device);
        rumble_device = NULL;
    }
    return rumble_device;
}

static void write_rumble_report(uint8_t low, uint8_t high, uint8_t left, uint8_t right) {
    pthread_mutex_lock(&rumble_lock);
    IOHIDDeviceRef device = open_rumble_device_locked();
    if (device) {
        uint8_t payload[4] = {low, high, left, right};
        IOReturn ret = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1,
                                            payload, sizeof(payload));
        log_line("write report [%u %u %u %u] ret=0x%08x", low, high, left, right, ret);
        if (ret != kIOReturnSuccess) {
            CFRelease(rumble_device);
            rumble_device = NULL;
        }
    }
    pthread_mutex_unlock(&rumble_lock);
}

static uint8_t peak4(uint8_t low, uint8_t high, uint8_t left, uint8_t right) {
    uint8_t peak = low;
    if (high > peak) peak = high;
    if (left > peak) peak = left;
    if (right > peak) peak = right;
    return peak;
}

static uint64_t now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return ((uint64_t)tv.tv_sec * 1000ULL) + ((uint64_t)tv.tv_usec / 1000ULL);
}

static void *rumble_worker_thread(void *opaque) {
    (void)opaque;
    uint8_t sent_low = 1;
    uint8_t sent_high = 1;
    uint8_t sent_left = 1;
    uint8_t sent_right = 1;
    uint64_t last_send_ms = 0;

    for (;;) {
        uint64_t now = now_ms();
        uint8_t main_low;
        uint8_t main_high;
        uint8_t main_power;
        uint64_t main_deadline;
        uint8_t trigger_left;
        uint8_t trigger_right;
        uint8_t trigger_power;
        uint64_t trigger_deadline;

        pthread_mutex_lock(&rumble_lock);
        main_low = rumble_main_low;
        main_high = rumble_main_high;
        main_power = rumble_main_power;
        main_deadline = rumble_main_deadline_ms;
        trigger_left = rumble_trigger_left;
        trigger_right = rumble_trigger_right;
        trigger_power = rumble_trigger_power;
        trigger_deadline = rumble_trigger_deadline_ms;
        pthread_mutex_unlock(&rumble_lock);

        if (now >= main_deadline || peak4(main_low, main_high, 0, 0) == 0 || main_power == 0) {
            main_low = main_high = 0;
        } else if (main_power >= 252) {
            main_low = main_high = 255;
        }

        if (now >= trigger_deadline || peak4(0, 0, trigger_left, trigger_right) == 0 || trigger_power == 0) {
            trigger_left = trigger_right = 0;
        } else if (trigger_power >= 252) {
            trigger_left = trigger_right = 255;
        }

        uint8_t low = main_low;
        uint8_t high = main_high;
        uint8_t left = trigger_left;
        uint8_t right = trigger_right;
        int active = (low || high || left || right);
        int changed = (low != sent_low || high != sent_high || left != sent_left || right != sent_right);
        int refresh_due = active && (now - last_send_ms >= 90);

        if (changed || refresh_due) {
            write_rumble_report(low, high, left, right);
            sent_low = low;
            sent_high = high;
            sent_left = left;
            sent_right = right;
            last_send_ms = now_ms();
        }

        usleep(active ? 12000 : 10000);
    }
    return NULL;
}

static uint8_t gta_curve_u8(uint8_t raw) {
    if (raw <= 3) return 0;
    if (raw >= 240) return 255;
    if (raw <= 32) return (uint8_t)(22U + ((uint32_t)raw * 14U) / 32U);
    if (raw <= 96) return (uint8_t)(36U + (((uint32_t)raw - 32U) * 18U) / 64U);
    if (raw <= 176) return (uint8_t)(54U + (((uint32_t)raw - 96U) * 6U) / 80U);
    return 60;
}

static void widen_main_motors(uint8_t *low, uint8_t *high) {
    if (*low && !*high) {
        uint8_t fill = (uint8_t)((*low * 2U) / 3U);
        if (fill < 28) fill = 28;
        *high = fill;
    } else if (*high && !*low) {
        uint8_t fill = (uint8_t)((*high * 2U) / 3U);
        if (fill < 28) fill = 28;
        *low = fill;
    }
}

static unsigned int main_min_hold_ms(uint8_t low, uint8_t high, uint8_t power) {
    uint8_t out_peak = peak4(low, high, 0, 0);
    if (out_peak == 0 || power == 0) return 0;
    if (out_peak >= 250 || power >= 240) return 420;
    if (out_peak >= 58 || power >= 176) return 320;
    if (out_peak >= 46 || power >= 80) return 240;
    return 180;
}

static void start_rumble_worker_locked(void) {
    if (rumble_worker_started) return;
    pthread_t thread;
    if (pthread_create(&thread, NULL, rumble_worker_thread, NULL) == 0) {
        pthread_detach(thread);
        rumble_worker_started = 1;
    }
}

static void send_main_rumble_bytes(uint8_t low, uint8_t high, uint8_t power,
                                   unsigned int duration_ms) {
    if (duration_ms == 0 && peak4(low, high, 0, 0) != 0 && power != 0) duration_ms = 250;
    if (duration_ms > 10000) duration_ms = 10000;
    uint64_t now = now_ms();

    pthread_mutex_lock(&rumble_lock);
    if (duration_ms == 0 || peak4(low, high, 0, 0) == 0 || power == 0) {
        if (now < rumble_main_min_deadline_ms && peak4(rumble_main_low, rumble_main_high, 0, 0) != 0) {
            rumble_main_deadline_ms = rumble_main_min_deadline_ms;
            log_line("defer main stop for %llu ms",
                     (unsigned long long)(rumble_main_min_deadline_ms - now));
        } else {
            rumble_main_low = 0;
            rumble_main_high = 0;
            rumble_main_power = 0;
            rumble_main_deadline_ms = now;
            rumble_main_min_deadline_ms = now;
        }
    } else {
        unsigned int min_hold = main_min_hold_ms(low, high, power);
        rumble_main_low = low;
        rumble_main_high = high;
        rumble_main_power = power;
        rumble_main_deadline_ms = now + duration_ms;
        rumble_main_min_deadline_ms = now + min_hold;
    }
    start_rumble_worker_locked();
    pthread_mutex_unlock(&rumble_lock);
}

static void send_trigger_rumble_bytes(uint8_t left, uint8_t right, uint8_t power,
                                      unsigned int duration_ms) {
    if (duration_ms == 0 && peak4(0, 0, left, right) != 0 && power != 0) duration_ms = 250;
    if (duration_ms > 10000) duration_ms = 10000;
    uint64_t now = now_ms();

    pthread_mutex_lock(&rumble_lock);
    if (duration_ms == 0 || peak4(0, 0, left, right) == 0 || power == 0) {
        rumble_trigger_left = 0;
        rumble_trigger_right = 0;
        rumble_trigger_power = 0;
        rumble_trigger_deadline_ms = now;
    } else {
        rumble_trigger_left = left;
        rumble_trigger_right = right;
        rumble_trigger_power = power;
        rumble_trigger_deadline_ms = now + duration_ms;
    }
    start_rumble_worker_locked();
    pthread_mutex_unlock(&rumble_lock);
}

static void send_all_rumble_stop(void) {
    uint64_t now = now_ms();
    pthread_mutex_lock(&rumble_lock);
    rumble_main_low = 0;
    rumble_main_high = 0;
    rumble_main_power = 0;
    rumble_main_deadline_ms = now;
    rumble_main_min_deadline_ms = now;
    rumble_trigger_left = 0;
    rumble_trigger_right = 0;
    rumble_trigger_power = 0;
    rumble_trigger_deadline_ms = now;
    start_rumble_worker_locked();
    pthread_mutex_unlock(&rumble_lock);
}

static void send_rumble_strength(int strength, unsigned int duration_ms) {
    if (strength < 0) strength = 0;
    if (strength > 100) strength = 100;
    uint8_t power = (uint8_t)((strength * 255) / 100);
    uint8_t value = gta_curve_u8(power);
    log_line("SDL_HapticRumble strength=%d raw=%u out=%u dur=%u",
             strength, power, value, duration_ms);
    send_main_rumble_bytes(value, value, power, duration_ms);
}

static int strength_from_float(float value) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return (int)(value * 100.0f + 0.5f);
}

int SDL_Init(uint32_t flags) {
    int (*f)(uint32_t) = sym("SDL_Init");
    return f ? f(flags) : -1;
}

const char *SDL_GetError(void) {
    const char *(*f)(void) = sym("SDL_GetError");
    return f ? f() : "8BitDo SDL shim could not load real SDL";
}

void SDL_SetHint(const char *name, const char *value) {
    void (*f)(const char *, const char *) = sym("SDL_SetHint");
    if (f) f(name, value);
}

uint32_t SDL_GetTicks(void) {
    uint32_t (*f)(void) = sym("SDL_GetTicks");
    return f ? f() : 0;
}

void SDL_Delay(uint32_t ms) {
    void (*f)(uint32_t) = sym("SDL_Delay");
    if (f) f(ms);
    else usleep(ms * 1000);
}

void SDL_Quit(void) {
    void (*f)(void) = sym("SDL_Quit");
    if (f) f();
}

char *SDL_getenv(const char *name) {
    char *(*f)(const char *) = sym("SDL_getenv");
    return f ? f(name) : getenv(name);
}

int SDL_RegisterEvents(int n) {
    int (*f)(int) = sym("SDL_RegisterEvents");
    return f ? f(n) : -1;
}

int SDL_PushEvent(void *event) {
    int (*f)(void *) = sym("SDL_PushEvent");
    return f ? f(event) : -1;
}

int SDL_WaitEventTimeout(void *event, int timeout) {
    int (*f)(void *, int) = sym("SDL_WaitEventTimeout");
    return f ? f(event, timeout) : 0;
}

int SDL_JoystickEventState(int state) {
    int (*f)(int) = sym("SDL_JoystickEventState");
    return f ? f(state) : 0;
}

void *SDL_JoystickOpen(int index) {
    void *(*f)(int) = sym("SDL_JoystickOpen");
    return f ? f(index) : NULL;
}

int SDL_NumJoysticks(void) {
    int (*f)(void) = sym("SDL_NumJoysticks");
    return f ? f() : 0;
}

const char *SDL_JoystickNameForIndex(int index) {
    const char *(*f)(int) = sym("SDL_JoystickNameForIndex");
    return f ? f(index) : NULL;
}

void SDL_JoystickClose(void *joy) {
    void (*f)(void *) = sym("SDL_JoystickClose");
    if (f) f(joy);
}

const char *SDL_JoystickName(void *joy) {
    const char *(*f)(void *) = sym("SDL_JoystickName");
    return f ? f(joy) : NULL;
}

int SDL_JoystickInstanceID(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickInstanceID");
    return f ? f(joy) : -1;
}

SDL_JoystickGUID SDL_JoystickGetGUID(void *joy) {
    SDL_JoystickGUID (*f)(void *) = sym("SDL_JoystickGetGUID");
    SDL_JoystickGUID zero = {{0}};
    return f ? f(joy) : zero;
}

SDL_JoystickGUID SDL_JoystickGetDeviceGUID(int index) {
    SDL_JoystickGUID (*f)(int) = sym("SDL_JoystickGetDeviceGUID");
    SDL_JoystickGUID zero = {{0}};
    return f ? f(index) : zero;
}

void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char *pszGUID, int cbGUID) {
    void (*f)(SDL_JoystickGUID, char *, int) = sym("SDL_JoystickGetGUIDString");
    if (f) f(guid, pszGUID, cbGUID);
}

int SDL_JoystickNumAxes(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickNumAxes");
    return f ? f(joy) : 0;
}

int SDL_JoystickNumButtons(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickNumButtons");
    return f ? f(joy) : 0;
}

int SDL_JoystickNumBalls(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickNumBalls");
    return f ? f(joy) : 0;
}

int SDL_JoystickNumHats(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickNumHats");
    return f ? f(joy) : 0;
}

int16_t SDL_JoystickGetAxis(void *joy, int axis) {
    int16_t (*f)(void *, int) = sym("SDL_JoystickGetAxis");
    return f ? f(joy, axis) : 0;
}

uint8_t SDL_JoystickGetHat(void *joy, int hat) {
    uint8_t (*f)(void *, int) = sym("SDL_JoystickGetHat");
    return f ? f(joy, hat) : 0;
}

uint8_t SDL_JoystickGetButton(void *joy, int button) {
    uint8_t (*f)(void *, int) = sym("SDL_JoystickGetButton");
    return f ? f(joy, button) : 0;
}

uint16_t SDL_JoystickGetProduct(void *joy) {
    uint16_t (*f)(void *) = sym("SDL_JoystickGetProduct");
    return f ? f(joy) : 0;
}

uint16_t SDL_JoystickGetProductVersion(void *joy) {
    uint16_t (*f)(void *) = sym("SDL_JoystickGetProductVersion");
    return f ? f(joy) : 0;
}

uint16_t SDL_JoystickGetVendor(void *joy) {
    uint16_t (*f)(void *) = sym("SDL_JoystickGetVendor");
    return f ? f(joy) : 0;
}

int SDL_JoystickGetType(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickGetType");
    return f ? f(joy) : 0;
}

const char *SDL_JoystickGetSerial(void *joy) {
    const char *(*f)(void *) = sym("SDL_JoystickGetSerial");
    return f ? f(joy) : NULL;
}

int SDL_JoystickIsHaptic(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickIsHaptic");
    int ret = f ? f(joy) : 0;
    return ret ? ret : 1;
}

int SDL_JoystickHasRumble(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickHasRumble");
    int ret = f ? f(joy) : 0;
    return ret ? ret : 1;
}

int SDL_JoystickHasRumbleTriggers(void *joy) {
    int (*f)(void *) = sym("SDL_JoystickHasRumbleTriggers");
    int ret = f ? f(joy) : 0;
    return ret ? ret : 1;
}

int SDL_JoystickRumble(void *joy, uint16_t low, uint16_t high, uint32_t duration_ms) {
    uint8_t low_raw = (uint8_t)(low >> 8);
    uint8_t high_raw = (uint8_t)(high >> 8);
    uint8_t low_out = gta_curve_u8(low_raw);
    uint8_t high_out = gta_curve_u8(high_raw);
    widen_main_motors(&low_out, &high_out);
    log_line("SDL_JoystickRumble raw=[%u %u] out=[%u %u] dur=%u",
             low_raw, high_raw, low_out, high_out, duration_ms);
    send_main_rumble_bytes(low_out, high_out,
                           peak4(low_raw, high_raw, 0, 0), duration_ms);
    int (*f)(void *, uint16_t, uint16_t, uint32_t) = sym("SDL_JoystickRumble");
    int ret = f ? f(joy, low, high, duration_ms) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_JoystickRumbleTriggers(void *joy, uint16_t left, uint16_t right, uint32_t duration_ms) {
    uint8_t left_raw = (uint8_t)(left >> 8);
    uint8_t right_raw = (uint8_t)(right >> 8);
    uint8_t left_out = gta_curve_u8(left_raw);
    uint8_t right_out = gta_curve_u8(right_raw);
    log_line("SDL_JoystickRumbleTriggers raw=[%u %u] out=[%u %u] dur=%u",
             left_raw, right_raw, left_out, right_out, duration_ms);
    send_trigger_rumble_bytes(left_out, right_out,
                              peak4(0, 0, left_raw, right_raw), duration_ms);
    int (*f)(void *, uint16_t, uint16_t, uint32_t) = sym("SDL_JoystickRumbleTriggers");
    int ret = f ? f(joy, left, right, duration_ms) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_IsGameController(int index) {
    int (*f)(int) = sym("SDL_IsGameController");
    return f ? f(index) : 0;
}

void *SDL_GameControllerOpen(int index) {
    void *(*f)(int) = sym("SDL_GameControllerOpen");
    return f ? f(index) : NULL;
}

void SDL_GameControllerClose(void *controller) {
    void (*f)(void *) = sym("SDL_GameControllerClose");
    if (f) f(controller);
}

const char *SDL_GameControllerName(void *controller) {
    const char *(*f)(void *) = sym("SDL_GameControllerName");
    return f ? f(controller) : NULL;
}

int16_t SDL_GameControllerGetAxis(void *controller, int axis) {
    int16_t (*f)(void *, int) = sym("SDL_GameControllerGetAxis");
    return f ? f(controller, axis) : 0;
}

uint8_t SDL_GameControllerGetButton(void *controller, int button) {
    uint8_t (*f)(void *, int) = sym("SDL_GameControllerGetButton");
    return f ? f(controller, button) : 0;
}

int SDL_GameControllerEventState(int state) {
    int (*f)(int) = sym("SDL_GameControllerEventState");
    return f ? f(state) : 0;
}

void *SDL_GameControllerGetJoystick(void *controller) {
    void *(*f)(void *) = sym("SDL_GameControllerGetJoystick");
    return f ? f(controller) : NULL;
}

int SDL_GameControllerHasRumble(void *controller) {
    int (*f)(void *) = sym("SDL_GameControllerHasRumble");
    int ret = f ? f(controller) : 0;
    return ret ? ret : 1;
}

int SDL_GameControllerHasRumbleTriggers(void *controller) {
    int (*f)(void *) = sym("SDL_GameControllerHasRumbleTriggers");
    int ret = f ? f(controller) : 0;
    return ret ? ret : 1;
}

int SDL_GameControllerRumble(void *controller, uint16_t low, uint16_t high, uint32_t duration_ms) {
    uint8_t low_raw = (uint8_t)(low >> 8);
    uint8_t high_raw = (uint8_t)(high >> 8);
    uint8_t low_out = gta_curve_u8(low_raw);
    uint8_t high_out = gta_curve_u8(high_raw);
    widen_main_motors(&low_out, &high_out);
    log_line("SDL_GameControllerRumble raw=[%u %u] out=[%u %u] dur=%u",
             low_raw, high_raw, low_out, high_out, duration_ms);
    send_main_rumble_bytes(low_out, high_out,
                           peak4(low_raw, high_raw, 0, 0), duration_ms);
    int (*f)(void *, uint16_t, uint16_t, uint32_t) = sym("SDL_GameControllerRumble");
    int ret = f ? f(controller, low, high, duration_ms) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_GameControllerRumbleTriggers(void *controller, uint16_t left, uint16_t right, uint32_t duration_ms) {
    uint8_t left_raw = (uint8_t)(left >> 8);
    uint8_t right_raw = (uint8_t)(right >> 8);
    uint8_t left_out = gta_curve_u8(left_raw);
    uint8_t right_out = gta_curve_u8(right_raw);
    log_line("SDL_GameControllerRumbleTriggers raw=[%u %u] out=[%u %u] dur=%u",
             left_raw, right_raw, left_out, right_out, duration_ms);
    send_trigger_rumble_bytes(left_out, right_out,
                              peak4(0, 0, left_raw, right_raw), duration_ms);
    int (*f)(void *, uint16_t, uint16_t, uint32_t) = sym("SDL_GameControllerRumbleTriggers");
    int ret = f ? f(controller, left, right, duration_ms) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_GameControllerAddMapping(const char *mapping) {
    int (*f)(const char *) = sym("SDL_GameControllerAddMapping");
    return f ? f(mapping) : -1;
}

void *SDL_HapticOpenFromJoystick(void *joy) {
    void *(*f)(void *) = sym("SDL_HapticOpenFromJoystick");
    void *ret = f ? f(joy) : NULL;
    return ret ? ret : FAKE_HAPTIC;
}

void SDL_HapticClose(void *haptic) {
    if (haptic == FAKE_HAPTIC) return;
    void (*f)(void *) = sym("SDL_HapticClose");
    if (f) f(haptic);
}

int SDL_HapticRumbleSupported(void *haptic) {
    int (*f)(void *) = sym("SDL_HapticRumbleSupported");
    int ret = (haptic != FAKE_HAPTIC && f) ? f(haptic) : 0;
    return ret ? ret : 1;
}

int SDL_HapticRumbleInit(void *haptic) {
    if (haptic == FAKE_HAPTIC) return 0;
    int (*f)(void *) = sym("SDL_HapticRumbleInit");
    int ret = f ? f(haptic) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_HapticRumblePlay(void *haptic, float strength, uint32_t length) {
    send_rumble_strength(strength_from_float(strength), length);
    if (haptic == FAKE_HAPTIC) return 0;
    int (*f)(void *, float, uint32_t) = sym("SDL_HapticRumblePlay");
    int ret = f ? f(haptic, strength, length) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_HapticRumbleStop(void *haptic) {
    send_all_rumble_stop();
    if (haptic == FAKE_HAPTIC) return 0;
    int (*f)(void *) = sym("SDL_HapticRumbleStop");
    int ret = f ? f(haptic) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_HapticQuery(void *haptic) {
    int (*f)(void *) = sym("SDL_HapticQuery");
    int ret = (haptic != FAKE_HAPTIC && f) ? f(haptic) : 0;
    return ret | SDL_HAPTIC_RUMBLE;
}

int SDL_HapticNewEffect(void *haptic, void *effect) {
    if (haptic == FAKE_HAPTIC) return 1;
    int (*f)(void *, void *) = sym("SDL_HapticNewEffect");
    return f ? f(haptic, effect) : 1;
}

int SDL_HapticUpdateEffect(void *haptic, int effect, void *data) {
    if (haptic == FAKE_HAPTIC) return 0;
    int (*f)(void *, int, void *) = sym("SDL_HapticUpdateEffect");
    return f ? f(haptic, effect, data) : 0;
}

int SDL_HapticRunEffect(void *haptic, int effect, uint32_t iterations) {
    if (haptic == FAKE_HAPTIC) {
        send_rumble_strength(70, 250);
        return 0;
    }
    int (*f)(void *, int, uint32_t) = sym("SDL_HapticRunEffect");
    int ret = f ? f(haptic, effect, iterations) : -1;
    return ret == 0 ? 0 : 0;
}

int SDL_HapticStopEffect(void *haptic, int effect) {
    send_all_rumble_stop();
    if (haptic == FAKE_HAPTIC) return 0;
    int (*f)(void *, int) = sym("SDL_HapticStopEffect");
    int ret = f ? f(haptic, effect) : -1;
    return ret == 0 ? 0 : 0;
}

void SDL_HapticDestroyEffect(void *haptic, int effect) {
    if (haptic == FAKE_HAPTIC) return;
    void (*f)(void *, int) = sym("SDL_HapticDestroyEffect");
    if (f) f(haptic, effect);
}

int SDL_HapticGetEffectStatus(void *haptic, int effect) {
    if (haptic == FAKE_HAPTIC) return 0;
    int (*f)(void *, int) = sym("SDL_HapticGetEffectStatus");
    return f ? f(haptic, effect) : 0;
}

int SDL_HapticNumAxes(void *haptic) {
    if (haptic == FAKE_HAPTIC) return 2;
    int (*f)(void *) = sym("SDL_HapticNumAxes");
    return f ? f(haptic) : 2;
}

int SDL_HapticPause(void *haptic) { int (*f)(void *) = sym("SDL_HapticPause"); return (haptic == FAKE_HAPTIC || !f) ? 0 : f(haptic); }
int SDL_HapticUnpause(void *haptic) { int (*f)(void *) = sym("SDL_HapticUnpause"); return (haptic == FAKE_HAPTIC || !f) ? 0 : f(haptic); }
int SDL_HapticSetGain(void *haptic, int gain) { int (*f)(void *, int) = sym("SDL_HapticSetGain"); return (haptic == FAKE_HAPTIC || !f) ? 0 : f(haptic, gain); }
int SDL_HapticSetAutocenter(void *haptic, int autocenter) { int (*f)(void *, int) = sym("SDL_HapticSetAutocenter"); return (haptic == FAKE_HAPTIC || !f) ? 0 : f(haptic, autocenter); }
int SDL_HapticStopAll(void *haptic) { send_all_rumble_stop(); int (*f)(void *) = sym("SDL_HapticStopAll"); return (haptic == FAKE_HAPTIC || !f) ? 0 : f(haptic); }
