#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static int get_int(IOHIDDeviceRef device, CFStringRef key) {
    CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
    int value = 0;
    if (ref) CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &value);
    return value;
}

static IOHIDDeviceRef find_vendor_interface(IOHIDManagerRef manager) {
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2dc8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x310a);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) return NULL;
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices) return NULL;

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    CFSetGetValues(devices, values);

    IOHIDDeviceRef found = NULL;
    for (CFIndex i = 0; i < count; i++) {
        IOHIDDeviceRef device = (IOHIDDeviceRef)values[i];
        int usage_page = get_int(device, CFSTR(kIOHIDPrimaryUsagePageKey));
        int usage = get_int(device, CFSTR(kIOHIDPrimaryUsageKey));
        if (usage_page == 0xff7a && usage == 1) {
            found = device;
            CFRetain(found);
            break;
        }
    }

    free(values);
    CFRelease(devices);
    return found;
}

static IOReturn send_rumble(IOHIDDeviceRef device, unsigned char weak, unsigned char strong) {
    unsigned char payload[63] = {0};
    payload[0] = 0x05;
    payload[1] = weak;
    payload[2] = strong;
    payload[3] = 0x00;
    payload[4] = 0x00;
    return IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 0x81, payload, sizeof(payload));
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_vendor_interface(manager);
    if (!device) {
        fprintf(stderr, "8BitDo 2dc8:310a vendor HID interface not found\n");
        CFRelease(manager);
        return 1;
    }

    IOReturn open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
    printf("open=0x%08x\n", open_result);
    if (open_result != kIOReturnSuccess) {
        CFRelease(device);
        CFRelease(manager);
        return 1;
    }

    IOReturn on = send_rumble(device, 0xff, 0xff);
    printf("rumble-on report_id=0x81 len=63 payload=05 ff ff 00 00 => 0x%08x\n", on);
    usleep(700 * 1000);
    IOReturn off = send_rumble(device, 0x00, 0x00);
    printf("rumble-off => 0x%08x\n", off);

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
