#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static IOHIDDeviceRef find_bt_device(IOHIDManagerRef manager) {
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2DC8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x301B);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);
    IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices || CFSetGetCount(devices) == 0) return NULL;
    const void **values = calloc((size_t)CFSetGetCount(devices), sizeof(void *));
    CFSetGetValues(devices, values);
    IOHIDDeviceRef device = (IOHIDDeviceRef)values[0];
    CFRetain(device);
    free(values);
    CFRelease(devices);
    return device;
}

static int get_int_prop(IOHIDElementRef element, CFStringRef key) {
    CFTypeRef ref = IOHIDElementGetProperty(element, key);
    int value = 0;
    if (ref && CFGetTypeID(ref) == CFNumberGetTypeID()) {
        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &value);
    }
    return value;
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_bt_device(manager);
    if (!device) {
        fprintf(stderr, "Bluetooth 8BitDo device not found\n");
        return 1;
    }
    printf("open => 0x%08x\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));

    CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, NULL, kIOHIDOptionsTypeNone);
    CFIndex count = elements ? CFArrayGetCount(elements) : 0;
    for (CFIndex i = 0; i < count; i++) {
        IOHIDElementRef el = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
        IOHIDElementType type = IOHIDElementGetType(el);
        uint32_t report_id = IOHIDElementGetReportID(el);
        uint32_t usage_page = IOHIDElementGetUsagePage(el);
        uint32_t usage = IOHIDElementGetUsage(el);
        if (type == kIOHIDElementTypeOutput || report_id == 5 || usage_page == 0x0f) {
            printf("element idx=%ld type=%d rid=%u usagePage=0x%x usage=0x%x reportSize=%d reportCount=%d cookie=%ld\n",
                (long)i, type, report_id, usage_page, usage,
                get_int_prop(el, CFSTR(kIOHIDElementReportSizeKey)),
                get_int_prop(el, CFSTR(kIOHIDElementReportCountKey)),
                (long)IOHIDElementGetCookie(el));
        }
        if (type == kIOHIDElementTypeOutput && report_id == 1) {
            IOHIDValueRef value = IOHIDValueCreateWithIntegerValue(kCFAllocatorDefault, el, 0, 0x64646464);
            IOReturn on = IOHIDDeviceSetValue(device, el, value);
            CFRelease(value);
            printf("  SetValue 0x64646464 => 0x%08x\n", on);
            usleep(300000);
            value = IOHIDValueCreateWithIntegerValue(kCFAllocatorDefault, el, 0, 0);
            IOReturn off = IOHIDDeviceSetValue(device, el, value);
            CFRelease(value);
            printf("  SetValue 0 => 0x%08x\n", off);
        }
    }

    if (elements) CFRelease(elements);
    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
