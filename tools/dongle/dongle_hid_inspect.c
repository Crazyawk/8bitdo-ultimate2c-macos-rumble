#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>
#include <stdlib.h>

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static int cf_int(CFTypeRef value) {
    int out = 0;
    if (value && CFGetTypeID(value) == CFNumberGetTypeID()) {
        CFNumberGetValue((CFNumberRef)value, kCFNumberIntType, &out);
    }
    return out;
}

static int prop_int(IOHIDDeviceRef device, CFStringRef key) {
    return cf_int(IOHIDDeviceGetProperty(device, key));
}

static void print_data(CFDataRef data) {
    if (!data) {
        printf("(null)");
        return;
    }
    const UInt8 *bytes = CFDataGetBytePtr(data);
    CFIndex len = CFDataGetLength(data);
    for (CFIndex i = 0; i < len; i++) printf("%02x", bytes[i]);
}

static void inspect_device(const void *value, void *context) {
    (void)context;
    IOHIDDeviceRef device = (IOHIDDeviceRef)value;
    CFStringRef product = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey));
    char product_buf[256] = {0};
    if (product) CFStringGetCString(product, product_buf, sizeof(product_buf), kCFStringEncodingUTF8);

    printf("device product=%s vid=0x%04x pid=0x%04x usagePage=0x%x usage=0x%x transport=",
        product_buf[0] ? product_buf : "(null)",
        prop_int(device, CFSTR(kIOHIDVendorIDKey)),
        prop_int(device, CFSTR(kIOHIDProductIDKey)),
        prop_int(device, CFSTR(kIOHIDPrimaryUsagePageKey)),
        prop_int(device, CFSTR(kIOHIDPrimaryUsageKey)));
    CFStringRef transport = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDTransportKey));
    if (transport) {
        char buf[128] = {0};
        CFStringGetCString(transport, buf, sizeof(buf), kCFStringEncodingUTF8);
        printf("%s", buf);
    } else {
        printf("(null)");
    }
    printf("\n");
    printf("  max input=%d output=%d feature=%d reportDescriptor=",
        prop_int(device, CFSTR(kIOHIDMaxInputReportSizeKey)),
        prop_int(device, CFSTR(kIOHIDMaxOutputReportSizeKey)),
        prop_int(device, CFSTR(kIOHIDMaxFeatureReportSizeKey)));
    print_data(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDReportDescriptorKey)));
    printf("\n");

    CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, NULL, kIOHIDOptionsTypeNone);
    CFIndex count = elements ? CFArrayGetCount(elements) : 0;
    for (CFIndex i = 0; i < count; i++) {
        IOHIDElementRef el = (IOHIDElementRef)CFArrayGetValueAtIndex(elements, i);
        IOHIDElementType type = IOHIDElementGetType(el);
        uint32_t rid = IOHIDElementGetReportID(el);
        uint32_t up = IOHIDElementGetUsagePage(el);
        uint32_t u = IOHIDElementGetUsage(el);
        if ((type >= kIOHIDElementTypeInput_Misc && type <= kIOHIDElementTypeInput_NULL) ||
            type == kIOHIDElementTypeOutput ||
            type == kIOHIDElementTypeFeature || rid != 0 || up >= 0xff00) {
            printf("  element idx=%ld type=%d rid=%u usagePage=0x%x usage=0x%x reportSize=%ld reportCount=%ld cookie=%ld\n",
                (long)i, type, rid, up, u,
                (long)IOHIDElementGetReportSize(el),
                (long)IOHIDElementGetReportCount(el),
                (long)IOHIDElementGetCookie(el));
        }
    }
    if (elements) CFRelease(elements);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2dc8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x310a);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    IOReturn open_result = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    printf("manager open=0x%08x\n", open_result);
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (devices) {
        printf("matched=%ld\n", (long)CFSetGetCount(devices));
        CFSetApplyFunction(devices, inspect_device, NULL);
        CFRelease(devices);
    } else {
        printf("matched=0\n");
    }
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
