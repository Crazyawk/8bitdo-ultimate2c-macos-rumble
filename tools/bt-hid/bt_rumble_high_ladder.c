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

    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) return NULL;
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

static void pulse(IOHIDDeviceRef device, unsigned char strength) {
    unsigned char payload[4] = {strength, strength, 0, 0};
    unsigned char off[4] = {0, 0, 0, 0};
    IOReturn on = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1, payload, 4);
    printf("raw_%u on => 0x%08x\\n", strength, on);
    fflush(stdout);
    usleep(300000);
    IOReturn stop = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1, off, 4);
    printf("raw_%u off => 0x%08x\\n", strength, stop);
    fflush(stdout);
    usleep(900000);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "BT device not found\\n");
        return 1;
    }
    printf("open => 0x%08x\\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));

    pulse(device, 180);
    pulse(device, 200);
    pulse(device, 220);
    pulse(device, 235);
    pulse(device, 245);
    pulse(device, 255);

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
