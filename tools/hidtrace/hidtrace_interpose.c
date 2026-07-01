#define _DARWIN_C_SOURCE
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    const void *replacement;
    const void *replacee;
} interpose_t;

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t resolver_lock = PTHREAD_MUTEX_INITIALIZER;
static void *iokit_handle;

typedef struct {
    IOHIDReportCallback original;
    void *original_context;
    IOHIDDeviceRef device;
} input_report_context_t;

static void *resolve_iokit_symbol(const char *name, const void *self);

static FILE *open_log_file(void) {
    return fopen("/tmp/8bitdo-hidtrace.log", "a");
}

static void send_udp_line(const char *line) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) return;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(39534);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sendto(fd, line, strlen(line), 0, (struct sockaddr *)&addr, sizeof(addr));
    close(fd);
}

static void log_line(const char *fmt, ...) {
    pthread_mutex_lock(&log_lock);
    char udp[2048];
    va_list udp_ap;
    va_start(udp_ap, fmt);
    vsnprintf(udp, sizeof(udp), fmt, udp_ap);
    va_end(udp_ap);
    send_udp_line(udp);

    va_list stderr_ap;
    va_start(stderr_ap, fmt);
    fputs("[hidtrace] ", stderr);
    vfprintf(stderr, fmt, stderr_ap);
    fputc('\n', stderr);
    fflush(stderr);
    va_end(stderr_ap);

    FILE *f = open_log_file();
    if (f) {
        va_list ap;
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    pthread_mutex_unlock(&log_lock);
}

static int cfnum_int(IOHIDDeviceRef device, CFStringRef key) {
    CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
    int value = 0;
    if (ref && CFGetTypeID(ref) == CFNumberGetTypeID()) {
        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &value);
    }
    return value;
}

static CFTypeRef traced_IOHIDDeviceGetProperty(IOHIDDeviceRef device, CFStringRef key) {
    static CFTypeRef (*orig)(IOHIDDeviceRef, CFStringRef);
    static _Thread_local int in_hook;
    static CFNumberRef spoofed_pid;
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDDeviceGetProperty",
                                    (const void *)traced_IOHIDDeviceGetProperty);
        if (!orig) return NULL;
    }
    if (in_hook) return orig(device, key);

    in_hook = 1;
    CFTypeRef ref = orig(device, key);
    if (key && CFEqual(key, CFSTR(kIOHIDProductIDKey))) {
        CFTypeRef vendor_ref = orig(device, CFSTR(kIOHIDVendorIDKey));
        int vendor = 0;
        int product = 0;
        if (vendor_ref && CFGetTypeID(vendor_ref) == CFNumberGetTypeID()) {
            CFNumberGetValue((CFNumberRef)vendor_ref, kCFNumberIntType, &vendor);
        }
        if (ref && CFGetTypeID(ref) == CFNumberGetTypeID()) {
            CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &product);
        }
        if (vendor == 0x2dc8 && product == 0x301b) {
            int spoof = 0x301a;
            if (!spoofed_pid) {
                spoofed_pid = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &spoof);
            }
            log_line("IOHIDDeviceGetProperty spoof pid 301b->301a for vid=2dc8");
            ref = spoofed_pid;
        }
    }
    in_hook = 0;
    return ref;
}

static void describe_device(IOHIDDeviceRef device, char *buf, size_t len) {
    int vid = cfnum_int(device, CFSTR(kIOHIDVendorIDKey));
    int pid = cfnum_int(device, CFSTR(kIOHIDProductIDKey));
    int page = cfnum_int(device, CFSTR(kIOHIDPrimaryUsagePageKey));
    int usage = cfnum_int(device, CFSTR(kIOHIDPrimaryUsageKey));
    snprintf(buf, len, "vid=%04x pid=%04x page=%04x usage=%04x", vid, pid, page, usage);
}

static void hex_dump(FILE *f, const uint8_t *data, CFIndex len) {
    CFIndex shown = len < 96 ? len : 96;
    for (CFIndex i = 0; i < shown; i++) {
        fprintf(f, " %02x", data[i]);
    }
    if (shown < len) {
        fprintf(f, " ...");
    }
}

static void log_report(const char *label, IOHIDDeviceRef device, IOHIDReportType type,
                       CFIndex report_id, const uint8_t *report, CFIndex report_len,
                       IOReturn result) {
    char dev[128];
    describe_device(device, dev, sizeof(dev));
    pthread_mutex_lock(&log_lock);
    char udp[4096];
    int pos = snprintf(udp, sizeof(udp),
                       "%s %s type=%ld id=%ld len=%ld result=0x%08x data:",
                       label, dev, (long)type, (long)report_id, (long)report_len, result);
    if (report && report_len > 0 && pos > 0) {
        CFIndex shown = report_len < 96 ? report_len : 96;
        for (CFIndex i = 0; i < shown && pos < (int)sizeof(udp) - 4; i++) {
            pos += snprintf(udp + pos, sizeof(udp) - (size_t)pos, " %02x", report[i]);
        }
        if (shown < report_len && pos < (int)sizeof(udp) - 5) {
            snprintf(udp + pos, sizeof(udp) - (size_t)pos, " ...");
        }
    }
    send_udp_line(udp);

    fprintf(stderr, "[hidtrace] %s %s type=%ld id=%ld len=%ld result=0x%08x data:",
            label, dev, (long)type, (long)report_id, (long)report_len, result);
    if (report && report_len > 0) {
        hex_dump(stderr, report, report_len);
    }
    fputc('\n', stderr);
    fflush(stderr);

    FILE *f = open_log_file();
    if (f) {
        fprintf(f, "%s %s type=%ld id=%ld len=%ld result=0x%08x data:",
                label, dev, (long)type, (long)report_id, (long)report_len, result);
        if (report && report_len > 0) {
            hex_dump(f, report, report_len);
        }
        fputc('\n', f);
        fclose(f);
    }
    pthread_mutex_unlock(&log_lock);
}

static int cf_dict_num(CFDictionaryRef dict, CFStringRef key) {
    CFTypeRef ref = CFDictionaryGetValue(dict, key);
    int value = -1;
    if (ref && CFGetTypeID(ref) == CFNumberGetTypeID()) {
        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &value);
    }
    return value;
}

static void log_hid_matching(CFArrayRef matches) {
    if (!matches || CFGetTypeID(matches) != CFArrayGetTypeID()) {
        log_line("IOHIDManagerSetDeviceMatchingMultiple matches=<not-array>");
        return;
    }

    CFIndex count = CFArrayGetCount(matches);
    char line[4096];
    int pos = snprintf(line, sizeof(line),
                       "IOHIDManagerSetDeviceMatchingMultiple count=%ld", (long)count);
    for (CFIndex i = 0; i < count && pos < (int)sizeof(line) - 80; i++) {
        CFTypeRef item = CFArrayGetValueAtIndex(matches, i);
        if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID()) {
            pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " [%ld]=<not-dict>",
                            (long)i);
            continue;
        }
        CFDictionaryRef dict = (CFDictionaryRef)item;
        int vid = cf_dict_num(dict, CFSTR(kIOHIDVendorIDKey));
        int pid = cf_dict_num(dict, CFSTR(kIOHIDProductIDKey));
        int page = cf_dict_num(dict, CFSTR(kIOHIDDeviceUsagePageKey));
        int usage = cf_dict_num(dict, CFSTR(kIOHIDDeviceUsageKey));
        if (page < 0) page = cf_dict_num(dict, CFSTR(kIOHIDPrimaryUsagePageKey));
        if (usage < 0) usage = cf_dict_num(dict, CFSTR(kIOHIDPrimaryUsageKey));
        pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                        " [%ld] vid=%04x pid=%04x page=%04x usage=%04x", (long)i,
                        vid < 0 ? 0 : vid, pid < 0 ? 0 : pid, page < 0 ? 0 : page,
                        usage < 0 ? 0 : usage);
    }
    log_line("%s", line);
}

static CFArrayRef copy_matching_with_ultimate2c_bt(CFArrayRef matches) {
    if (!matches || CFGetTypeID(matches) != CFArrayGetTypeID()) return matches;

    Boolean has_2dc8_301b = false;
    CFIndex count = CFArrayGetCount(matches);
    for (CFIndex i = 0; i < count; i++) {
        CFTypeRef item = CFArrayGetValueAtIndex(matches, i);
        if (!item || CFGetTypeID(item) != CFDictionaryGetTypeID()) continue;
        CFDictionaryRef dict = (CFDictionaryRef)item;
        if (cf_dict_num(dict, CFSTR(kIOHIDVendorIDKey)) == 0x2dc8 &&
            cf_dict_num(dict, CFSTR(kIOHIDProductIDKey)) == 0x301b) {
            has_2dc8_301b = true;
            break;
        }
    }
    if (has_2dc8_301b) return matches;

    CFMutableArrayRef copy = CFArrayCreateMutableCopy(kCFAllocatorDefault, 0, matches);
    if (!copy) return matches;

    int vendor = 0x2dc8;
    int product = 0x301b;
    CFNumberRef vendor_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
    CFNumberRef product_ref = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &product);
    if (!vendor_ref || !product_ref) {
        if (vendor_ref) CFRelease(vendor_ref);
        if (product_ref) CFRelease(product_ref);
        CFRelease(copy);
        return matches;
    }
    const void *keys[] = { CFSTR(kIOHIDVendorIDKey), CFSTR(kIOHIDProductIDKey) };
    const void *values[] = { vendor_ref, product_ref };
    CFDictionaryRef dict = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    if (dict) {
        CFArrayAppendValue(copy, dict);
        CFRelease(dict);
        log_line("IOHIDManagerSetDeviceMatchingMultiple appended vid=2dc8 pid=301b");
    }
    CFRelease(vendor_ref);
    CFRelease(product_ref);
    return copy;
}

static void *resolve_iokit_symbol(const char *name, const void *self) {
    pthread_mutex_lock(&resolver_lock);
    if (!iokit_handle) {
        iokit_handle = dlopen("/System/Library/Frameworks/IOKit.framework/Versions/A/IOKit",
                              RTLD_LAZY | RTLD_LOCAL);
        if (!iokit_handle) {
            iokit_handle = dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",
                                  RTLD_LAZY | RTLD_LOCAL);
        }
    }

    void *symbol = iokit_handle ? dlsym(iokit_handle, name) : NULL;
    if (!symbol || symbol == self) {
        void *next = dlsym(RTLD_NEXT, name);
        if (next && next != self) {
            symbol = next;
        }
    }

    if (!symbol || symbol == self) {
        uintptr_t target_offset = 0;
        if (strcmp(name, "IOHIDDeviceGetProperty") == 0) target_offset = 0x00046778;
        else if (strcmp(name, "IOHIDDeviceOpen") == 0) target_offset = 0x0000EE54;
        else if (strcmp(name, "IOHIDDeviceGetReport") == 0) target_offset = 0x0000EEA8;
        else if (strcmp(name, "IOHIDDeviceSetValue") == 0) target_offset = 0x000481B4;
        else if (strcmp(name, "IOHIDDeviceRegisterInputReportCallback") == 0) target_offset = 0x00048960;
        else if (strcmp(name, "IOHIDDeviceSetReport") == 0) target_offset = 0x000489CC;
        else if (strcmp(name, "IOHIDManagerCreate") == 0) target_offset = 0x000053F0;
        else if (strcmp(name, "IOHIDManagerOpen") == 0) target_offset = 0x000ACAD8;
        else if (strcmp(name, "IOHIDManagerSetDeviceMatchingMultiple") == 0) target_offset = 0x00005570;
        else if (strcmp(name, "IOHIDManagerRegisterDeviceMatchingCallback") == 0) target_offset = 0x00005DC8;
        else if (strcmp(name, "IOHIDManagerScheduleWithRunLoop") == 0) target_offset = 0x0004AD94;

        if (target_offset) {
            Dl_info info;
            if (dladdr((const void *)IOHIDDeviceSetReportWithCallback, &info) && info.dli_fbase) {
                symbol = (void *)((uintptr_t)info.dli_fbase + target_offset);
            }
        }
    }
    pthread_mutex_unlock(&resolver_lock);

    if (!symbol || symbol == self) {
        log_line("resolver failed for %s symbol=%p self=%p", name, symbol, self);
        return NULL;
    }
    return symbol;
}

static void traced_input_report_callback(void *context, IOReturn result, void *sender,
                                         IOHIDReportType type, uint32_t report_id,
                                         uint8_t *report, CFIndex report_len) {
    input_report_context_t *ctx = (input_report_context_t *)context;
    IOHIDDeviceRef device = ctx && ctx->device ? ctx->device : (IOHIDDeviceRef)sender;
    static uint8_t last_report[128];
    static CFIndex last_len = 0;
    static uint32_t last_id = 0;
    static unsigned long repeat_count = 0;
    Boolean changed = report_len != last_len || report_id != last_id ||
                      (report_len > 0 && report_len <= (CFIndex)sizeof(last_report) &&
                       memcmp(last_report, report, (size_t)report_len) != 0);
    if (changed || repeat_count < 3 || repeat_count % 500 == 0) {
        log_report("IOHIDInputReportCallback", device, type, report_id, report, report_len, result);
    }
    if (report_len > 0 && report_len <= (CFIndex)sizeof(last_report)) {
        memcpy(last_report, report, (size_t)report_len);
        last_len = report_len;
        last_id = report_id;
    }
    repeat_count = changed ? 0 : repeat_count + 1;
    if (ctx && ctx->original) {
        int vid = cfnum_int(device, CFSTR(kIOHIDVendorIDKey));
        int pid = cfnum_int(device, CFSTR(kIOHIDProductIDKey));
        if (vid == 0x2dc8 && pid == 0x301b && report && report_len > 0 && report_len < 64) {
            uint8_t padded[64];
            memset(padded, 0, sizeof(padded));
            memcpy(padded, report, (size_t)report_len);
            ctx->original(ctx->original_context, result, sender, type, report_id, padded,
                          sizeof(padded));
        } else {
            ctx->original(ctx->original_context, result, sender, type, report_id, report,
                          report_len);
        }
    }
}

static void traced_IOHIDDeviceRegisterInputReportCallback(IOHIDDeviceRef device,
                                                         uint8_t *report,
                                                         CFIndex report_len,
                                                         IOHIDReportCallback callback,
                                                         void *context) {
    static void (*orig)(IOHIDDeviceRef, uint8_t *, CFIndex, IOHIDReportCallback, void *);
    static _Thread_local int in_hook;
    char dev[128];
    describe_device(device, dev, sizeof(dev));
    log_line("IOHIDDeviceRegisterInputReportCallback %s len=%ld callback=%p context=%p",
             dev, (long)report_len, callback, context);
    if (in_hook) {
        log_line("IOHIDDeviceRegisterInputReportCallback recursion blocked");
        return;
    }
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDDeviceRegisterInputReportCallback",
                                    (const void *)traced_IOHIDDeviceRegisterInputReportCallback);
        if (!orig) return;
    }
    input_report_context_t *wrapped = NULL;
    IOHIDReportCallback effective_callback = NULL;
    void *effective_context = NULL;
    if (callback) {
        wrapped = calloc(1, sizeof(*wrapped));
        if (wrapped) {
            wrapped->original = callback;
            wrapped->original_context = context;
            wrapped->device = device;
            effective_callback = traced_input_report_callback;
            effective_context = wrapped;
        } else {
            effective_callback = callback;
            effective_context = context;
        }
    }
    in_hook = 1;
    orig(device, report, report_len, effective_callback, effective_context);
    in_hook = 0;
}

static IOReturn traced_IOHIDDeviceOpen(IOHIDDeviceRef device, IOHIDOptionsType options) {
    static IOReturn (*orig)(IOHIDDeviceRef, IOHIDOptionsType);
    static _Thread_local int in_hook;
    if (in_hook) {
        log_line("IOHIDDeviceOpen recursion blocked");
        return kIOReturnError;
    }
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDDeviceOpen", (const void *)traced_IOHIDDeviceOpen);
        if (!orig) return kIOReturnError;
    }
    in_hook = 1;
    IOReturn result = orig(device, options);
    in_hook = 0;
    char dev[128];
    describe_device(device, dev, sizeof(dev));
    log_line("IOHIDDeviceOpen %s options=0x%x result=0x%08x", dev, options, result);
    return result;
}

static IOHIDManagerRef traced_IOHIDManagerCreate(CFAllocatorRef allocator,
                                                 IOOptionBits options) {
    static IOHIDManagerRef (*orig)(CFAllocatorRef, IOOptionBits);
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDManagerCreate",
                                    (const void *)traced_IOHIDManagerCreate);
        if (!orig) return NULL;
    }
    IOHIDManagerRef manager = orig(allocator, options);
    log_line("IOHIDManagerCreate options=0x%x manager=%p", options, manager);
    return manager;
}

static IOReturn traced_IOHIDManagerOpen(IOHIDManagerRef manager, IOOptionBits options) {
    static IOReturn (*orig)(IOHIDManagerRef, IOOptionBits);
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDManagerOpen",
                                    (const void *)traced_IOHIDManagerOpen);
        if (!orig) return kIOReturnError;
    }
    IOReturn result = orig(manager, options);
    log_line("IOHIDManagerOpen manager=%p options=0x%x result=0x%08x", manager, options,
             result);
    return result;
}

static void traced_IOHIDManagerSetDeviceMatchingMultiple(IOHIDManagerRef manager,
                                                        CFArrayRef multiple) {
    static void (*orig)(IOHIDManagerRef, CFArrayRef);
    log_hid_matching(multiple);
    CFArrayRef effective = copy_matching_with_ultimate2c_bt(multiple);
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDManagerSetDeviceMatchingMultiple",
                                    (const void *)traced_IOHIDManagerSetDeviceMatchingMultiple);
        if (!orig) {
            if (effective && effective != multiple) CFRelease(effective);
            return;
        }
    }
    orig(manager, effective);
    if (effective && effective != multiple) CFRelease(effective);
}

static void traced_IOHIDManagerRegisterDeviceMatchingCallback(IOHIDManagerRef manager,
                                                            IOHIDDeviceCallback callback,
                                                            void *context) {
    static void (*orig)(IOHIDManagerRef, IOHIDDeviceCallback, void *);
    log_line("IOHIDManagerRegisterDeviceMatchingCallback manager=%p callback=%p context=%p",
             manager, callback, context);
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDManagerRegisterDeviceMatchingCallback",
                                    (const void *)traced_IOHIDManagerRegisterDeviceMatchingCallback);
        if (!orig) return;
    }
    orig(manager, callback, context);
}

static void traced_IOHIDManagerScheduleWithRunLoop(IOHIDManagerRef manager,
                                                  CFRunLoopRef runLoop,
                                                  CFStringRef runLoopMode) {
    static void (*orig)(IOHIDManagerRef, CFRunLoopRef, CFStringRef);
    log_line("IOHIDManagerScheduleWithRunLoop manager=%p runLoop=%p", manager, runLoop);
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDManagerScheduleWithRunLoop",
                                    (const void *)traced_IOHIDManagerScheduleWithRunLoop);
        if (!orig) return;
    }
    orig(manager, runLoop, runLoopMode);
}

static IOReturn traced_IOHIDDeviceSetReport(IOHIDDeviceRef device, IOHIDReportType type,
                                            CFIndex report_id, const uint8_t *report,
                                            CFIndex report_len) {
    static IOReturn (*orig)(IOHIDDeviceRef, IOHIDReportType, CFIndex, const uint8_t *, CFIndex);
    static _Thread_local int in_hook;
    if (in_hook) {
        log_report("IOHIDDeviceSetReport-recursion-blocked", device, type, report_id, report,
                   report_len, kIOReturnError);
        return kIOReturnError;
    }
    log_report("IOHIDDeviceSetReport-pre", device, type, report_id, report, report_len, 0);
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDDeviceSetReport",
                                    (const void *)traced_IOHIDDeviceSetReport);
        if (!orig) {
            IOReturn fallback = IOHIDDeviceSetReportWithCallback(
                device, type, report_id, report, report_len, 5000, NULL, NULL);
            log_report("IOHIDDeviceSetReport-async-fallback", device, type, report_id, report,
                       report_len, fallback);
            return fallback;
        }
    }
    in_hook = 1;
    IOReturn result = orig(device, type, report_id, report, report_len);
    in_hook = 0;
    log_report("IOHIDDeviceSetReport", device, type, report_id, report, report_len, result);
    return result;
}

static IOReturn traced_IOHIDDeviceGetReport(IOHIDDeviceRef device, IOHIDReportType type,
                                            CFIndex report_id, uint8_t *report,
                                            CFIndex *report_len) {
    static IOReturn (*orig)(IOHIDDeviceRef, IOHIDReportType, CFIndex, uint8_t *, CFIndex *);
    static _Thread_local int in_hook;
    if (in_hook) {
        log_report("IOHIDDeviceGetReport-recursion-blocked", device, type, report_id, report,
                   report_len ? *report_len : 0, kIOReturnError);
        return kIOReturnError;
    }
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDDeviceGetReport",
                                    (const void *)traced_IOHIDDeviceGetReport);
        if (!orig) return kIOReturnError;
    }
    CFIndex before = report_len ? *report_len : 0;
    log_report("IOHIDDeviceGetReport-pre", device, type, report_id, report, before, 0);
    in_hook = 1;
    IOReturn result = orig(device, type, report_id, report, report_len);
    in_hook = 0;
    log_report("IOHIDDeviceGetReport", device, type, report_id, report,
               report_len ? *report_len : before, result);
    return result;
}

static IOReturn traced_IOHIDDeviceSetValue(IOHIDDeviceRef device, IOHIDElementRef element,
                                           IOHIDValueRef value) {
    static IOReturn (*orig)(IOHIDDeviceRef, IOHIDElementRef, IOHIDValueRef);
    static _Thread_local int in_hook;
    if (in_hook) {
        log_line("IOHIDDeviceSetValue recursion blocked");
        return kIOReturnError;
    }
    if (!orig) {
        orig = resolve_iokit_symbol("IOHIDDeviceSetValue",
                                    (const void *)traced_IOHIDDeviceSetValue);
        if (!orig) return kIOReturnError;
    }
    in_hook = 1;
    IOReturn result = orig(device, element, value);
    in_hook = 0;
    char dev[128];
    describe_device(device, dev, sizeof(dev));
    log_line("IOHIDDeviceSetValue %s element_report=%u page=%u usage=%u int=%ld result=0x%08x",
             dev,
             IOHIDElementGetReportID(element),
             IOHIDElementGetUsagePage(element),
             IOHIDElementGetUsage(element),
             (long)IOHIDValueGetIntegerValue(value),
             result);
    return result;
}

__attribute__((constructor))
static void hidtrace_init(void) {
    log_line("=== hidtrace loaded pid=%d ===", getpid());
}

__attribute__((used)) static const interpose_t interposers[]
__attribute__((section("__DATA,__interpose"))) = {
    { (const void *)traced_IOHIDDeviceGetProperty, (const void *)IOHIDDeviceGetProperty },
    { (const void *)traced_IOHIDManagerCreate, (const void *)IOHIDManagerCreate },
    { (const void *)traced_IOHIDManagerOpen, (const void *)IOHIDManagerOpen },
    { (const void *)traced_IOHIDManagerSetDeviceMatchingMultiple,
      (const void *)IOHIDManagerSetDeviceMatchingMultiple },
    { (const void *)traced_IOHIDManagerRegisterDeviceMatchingCallback,
      (const void *)IOHIDManagerRegisterDeviceMatchingCallback },
    { (const void *)traced_IOHIDManagerScheduleWithRunLoop,
      (const void *)IOHIDManagerScheduleWithRunLoop },
    { (const void *)traced_IOHIDDeviceOpen, (const void *)IOHIDDeviceOpen },
    { (const void *)traced_IOHIDDeviceSetReport, (const void *)IOHIDDeviceSetReport },
    { (const void *)traced_IOHIDDeviceGetReport, (const void *)IOHIDDeviceGetReport },
    { (const void *)traced_IOHIDDeviceSetValue, (const void *)IOHIDDeviceSetValue },
    { (const void *)traced_IOHIDDeviceRegisterInputReportCallback,
      (const void *)IOHIDDeviceRegisterInputReportCallback },
};
