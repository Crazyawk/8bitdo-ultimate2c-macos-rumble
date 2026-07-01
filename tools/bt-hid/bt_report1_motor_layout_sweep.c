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

static void pulse(IOHIDDeviceRef device, const char *label, const unsigned char payload[4]) {
    unsigned char off[4] = {0, 0, 0, 0};
    IOReturn on = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1, payload, 4);
    printf("%s on => 0x%08x [%02x %02x %02x %02x]\n", label, on,
           payload[0], payload[1], payload[2], payload[3]);
    fflush(stdout);
    usleep(350000);
    IOReturn stop = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1, off, 4);
    printf("%s off => 0x%08x\n", label, stop);
    fflush(stdout);
    usleep(900000);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "BT device not found\n");
        return 1;
    }
    printf("open => 0x%08x\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));

    const unsigned char all4[4] = {0xff, 0xff, 0xff, 0xff};
    const unsigned char pi_order[4] = {0xff, 0xff, 0x00, 0x00};
    const unsigned char xpad_mid[4] = {0x00, 0xff, 0xff, 0x00};
    const unsigned char dongle_prefix[4] = {0x05, 0xff, 0xff, 0x00};
    const unsigned char alt_prefix[4] = {0x08, 0x00, 0xff, 0xff};
    const unsigned char low_hi[4] = {0x00, 0x00, 0xff, 0xff};
    const unsigned char triggers_first[4] = {0xff, 0x00, 0xff, 0x00};
    const unsigned char motors_split[4] = {0x00, 0xff, 0x00, 0xff};

    pulse(device, "all4", all4);
    pulse(device, "pi_order", pi_order);
    pulse(device, "xpad_mid", xpad_mid);
    pulse(device, "dongle_prefix", dongle_prefix);
    pulse(device, "alt_prefix", alt_prefix);
    pulse(device, "low_hi", low_hi);
    pulse(device, "triggers_first", triggers_first);
    pulse(device, "motors_split", motors_split);

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
