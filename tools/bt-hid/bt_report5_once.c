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

static IOHIDDeviceRef find_device(IOHIDManagerRef manager) {
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(
        kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,
        &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2dc8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x301b);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        return NULL;
    }
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices || CFSetGetCount(devices) == 0) {
        if (devices) CFRelease(devices);
        return NULL;
    }

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    CFSetGetValues(devices, values);
    IOHIDDeviceRef device = (IOHIDDeviceRef)values[0];
    CFRetain(device);
    free(values);
    CFRelease(devices);
    return device;
}

static void send_report(IOHIDDeviceRef device, const char *label, const unsigned char *bytes, size_t len) {
    IOReturn result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 5, bytes, len);
    printf("%s len=%zu => 0x%08x", label, len, result);
    for (size_t i = 0; i < len; i++) {
        printf(" %02x", bytes[i]);
    }
    printf("\n");
    fflush(stdout);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "8BitDo Bluetooth HID device 2dc8:301b not found\n");
        return 1;
    }

    IOReturn open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
    printf("open => 0x%08x\n", open_result);

    const unsigned char on_payload[] = {100, 100, 100, 100};
    const unsigned char off_payload[] = {0, 0, 0, 0};
    send_report(device, "report5 on", on_payload, sizeof(on_payload));
    usleep(700000);
    send_report(device, "report5 off", off_payload, sizeof(off_payload));

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
