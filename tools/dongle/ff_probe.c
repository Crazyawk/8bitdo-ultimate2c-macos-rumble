#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <dlfcn.h>
#include <stdio.h>

typedef int (*FFIsForceFeedbackFn)(io_service_t service);
typedef int (*FFCreateDeviceFn)(io_service_t service, void **device);
typedef int (*FFReleaseDeviceFn)(void *device);
typedef int (*FFDeviceGetForceFeedbackCapabilitiesFn)(void *device, void *capabilities);

typedef struct {
    NumVersion ffSpecVer;
    unsigned int supportedEffects;
    unsigned int emulatedEffects;
    unsigned int subType;
    unsigned int numFfAxes;
    unsigned char ffAxes[32];
    unsigned int storageCapacity;
    unsigned int playbackCapacity;
    NumVersion firmwareVer;
    NumVersion hardwareVer;
    NumVersion driverVer;
} FFCAPABILITIES;

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static void print_cf_string(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFStringGetTypeID()) {
        printf("(null)");
        return;
    }
    char buffer[512];
    if (CFStringGetCString((CFStringRef)value, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        printf("%s", buffer);
    } else {
        printf("(string)");
    }
}

static void probe_class(const char *class_name, FFIsForceFeedbackFn FFIsForceFeedback,
                        FFCreateDeviceFn FFCreateDevice,
                        FFReleaseDeviceFn FFReleaseDevice,
                        FFDeviceGetForceFeedbackCapabilitiesFn FFDeviceGetForceFeedbackCapabilities) {
    CFMutableDictionaryRef match = IOServiceMatching(class_name);
    if (!match) {
        printf("class %s: no matching dictionary\n", class_name);
        return;
    }
    CFMutableDictionaryRef props = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(props, CFSTR(kIOHIDVendorIDKey), 0x2dc8);
    set_int(props, CFSTR(kIOHIDProductIDKey), 0x301b);
    CFDictionarySetValue(match, CFSTR("IOPropertyMatch"), props);
    CFRelease(props);

    io_iterator_t iter = 0;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS) {
        printf("class %s: matching failed 0x%08x\n", class_name, kr);
        return;
    }

    int count = 0;
    io_service_t service;
    while ((service = IOIteratorNext(iter))) {
        count++;
        CFTypeRef product = IORegistryEntryCreateCFProperty(service, CFSTR(kIOHIDProductKey), kCFAllocatorDefault, 0);
        if (!product) product = IORegistryEntryCreateCFProperty(service, CFSTR("Product"), kCFAllocatorDefault, 0);
        CFTypeRef transport = IORegistryEntryCreateCFProperty(service, CFSTR(kIOHIDTransportKey), kCFAllocatorDefault, 0);
        if (!transport) transport = IORegistryEntryCreateCFProperty(service, CFSTR("Transport"), kCFAllocatorDefault, 0);
        printf("class=%s service=%u product=", class_name, service);
        print_cf_string(product);
        printf(" transport=");
        print_cf_string(transport);
        int is_ff = FFIsForceFeedback(service);
        printf(" FFIsForceFeedback=%d\n", is_ff);

        void *device = NULL;
        int create = FFCreateDevice(service, &device);
        printf("  FFCreateDevice=%d device=%p\n", create, device);
        if (device) {
            FFCAPABILITIES caps = {0};
            int cap_result = FFDeviceGetForceFeedbackCapabilities(device, &caps);
            printf("  capabilities=%d supported=0x%08x emulated=0x%08x subtype=%u axes=%u storage=%u playback=%u\n",
                cap_result, caps.supportedEffects, caps.emulatedEffects, caps.subType,
                caps.numFfAxes, caps.storageCapacity, caps.playbackCapacity);
            FFReleaseDevice(device);
        }
        if (product) CFRelease(product);
        if (transport) CFRelease(transport);
        IOObjectRelease(service);
    }
    IOObjectRelease(iter);
    printf("class=%s matched services=%d\n", class_name, count);
}

int main(void) {
    void *ff = dlopen("/System/Library/Frameworks/ForceFeedback.framework/ForceFeedback", RTLD_LAZY);
    if (!ff) {
        fprintf(stderr, "dlopen ForceFeedback failed: %s\n", dlerror());
        return 1;
    }

    FFIsForceFeedbackFn FFIsForceFeedback = (FFIsForceFeedbackFn)dlsym(ff, "FFIsForceFeedback");
    FFCreateDeviceFn FFCreateDevice = (FFCreateDeviceFn)dlsym(ff, "FFCreateDevice");
    FFReleaseDeviceFn FFReleaseDevice = (FFReleaseDeviceFn)dlsym(ff, "FFReleaseDevice");
    FFDeviceGetForceFeedbackCapabilitiesFn FFDeviceGetForceFeedbackCapabilities =
        (FFDeviceGetForceFeedbackCapabilitiesFn)dlsym(ff, "FFDeviceGetForceFeedbackCapabilities");
    if (!FFIsForceFeedback || !FFCreateDevice || !FFReleaseDevice || !FFDeviceGetForceFeedbackCapabilities) {
        fprintf(stderr, "missing ForceFeedback symbols\n");
        return 2;
    }

    probe_class("IOHIDDevice", FFIsForceFeedback, FFCreateDevice, FFReleaseDevice, FFDeviceGetForceFeedbackCapabilities);
    probe_class("IOHIDResource", FFIsForceFeedback, FFCreateDevice, FFReleaseDevice, FFDeviceGetForceFeedbackCapabilities);
    probe_class("IOHIDUserDevice", FFIsForceFeedback, FFCreateDevice, FFReleaseDevice, FFDeviceGetForceFeedbackCapabilities);
    probe_class("IOHIDInterface", FFIsForceFeedback, FFCreateDevice, FFReleaseDevice, FFDeviceGetForceFeedbackCapabilities);
    probe_class("AppleUserHIDEventService", FFIsForceFeedback, FFCreateDevice, FFReleaseDevice, FFDeviceGetForceFeedbackCapabilities);
    return 0;
}
