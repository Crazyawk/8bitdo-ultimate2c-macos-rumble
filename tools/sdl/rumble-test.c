#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static CFMutableDictionaryRef int_match(CFStringRef key, int value) {
    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
    return dict;
}

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

int main(int argc, char **argv) {
    int strength = argc > 1 ? atoi(argv[1]) : 70;
    int duration_ms = argc > 2 ? atoi(argv[2]) : 700;
    if (strength < 0) strength = 0;
    if (strength > 100) strength = 100;

    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    if (!manager) {
        fprintf(stderr, "Could not create HID manager\n");
        return 1;
    }

    CFMutableDictionaryRef match = int_match(CFSTR(kIOHIDVendorIDKey), 0x2DC8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x301B);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    IOReturn open_result = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    if (open_result != kIOReturnSuccess) {
        fprintf(stderr, "Could not open HID manager: 0x%08x\n", open_result);
        CFRelease(manager);
        return 1;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices || CFSetGetCount(devices) == 0) {
        fprintf(stderr, "8BitDo Ultimate 2C Wireless not found over Bluetooth\n");
        if (devices) CFRelease(devices);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        return 1;
    }

    CFIndex device_count = CFSetGetCount(devices);
    const void **device_values = calloc((size_t)device_count, sizeof(void *));
    if (!device_values) {
        fprintf(stderr, "Could not allocate device list\n");
        CFRelease(devices);
        IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
        CFRelease(manager);
        return 1;
    }
    CFSetGetValues(devices, device_values);
    IOHIDDeviceRef device = (IOHIDDeviceRef)device_values[0];
    IOReturn device_open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);

    unsigned char on[] = { strength, strength, strength, strength };
    unsigned char off[] = { 0, 0, 0, 0 };

    IOReturn on_result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 5, on, sizeof(on));
    usleep(duration_ms * 1000);
    IOReturn off_result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 5, off, sizeof(off));

    printf("open=0x%08x report_id=5 payload_len=%zu strength=%d duration_ms=%d on=0x%08x off=0x%08x\n",
        device_open_result, sizeof(on), strength, duration_ms, on_result, off_result);

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    free(device_values);
    CFRelease(devices);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return (on_result == kIOReturnSuccess && off_result == kIOReturnSuccess) ? 0 : 2;
}
