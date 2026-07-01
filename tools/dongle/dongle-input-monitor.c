#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>
#include <stdlib.h>

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static void input_cb(void *context, IOReturn result, void *sender,
                     IOHIDReportType type, uint32_t report_id,
                     uint8_t *report, CFIndex report_length) {
    (void)context; (void)sender; (void)type;
    printf("input result=0x%08x id=%u len=%ld", result, report_id, (long)report_length);
    for (CFIndex i = 0; i < report_length && i < 24; i++) printf(" %02x", report[i]);
    printf("\n");
    fflush(stdout);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2DC8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x310A);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices) {
        fprintf(stderr, "No dongle HID devices found\n");
        return 1;
    }

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    CFSetGetValues(devices, values);

    for (CFIndex i = 0; i < count; i++) {
        IOHIDDeviceRef device = (IOHIDDeviceRef)values[i];
        IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
        uint8_t *buffer = calloc(2048, 1);
        IOHIDDeviceRegisterInputReportCallback(device, buffer, 2048, input_cb, buffer);
        IOHIDDeviceScheduleWithRunLoop(device, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    }

    printf("monitoring %ld dongle HID interfaces for 10 seconds...\n", (long)count);
    fflush(stdout);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 10.0, false);
    return 0;
}
