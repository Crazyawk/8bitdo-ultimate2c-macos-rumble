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
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
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

static void send_one(IOHIDDeviceRef device, const char *label, CFIndex rid,
                     const unsigned char *payload, size_t len) {
    IOReturn result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, rid, payload, len);
    printf("%s rid=%ld len=%zu => 0x%08x", label, (long)rid, len, result);
    for (size_t i = 0; i < len; i++) printf(" %02x", payload[i]);
    printf("\n");
    fflush(stdout);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "BT device not found\n");
        return 1;
    }
    printf("open => 0x%08x\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));
    unsigned char p1[] = {100};
    unsigned char z1[] = {0};
    unsigned char p4[] = {100, 100, 100, 100};
    unsigned char z4[] = {0, 0, 0, 0};
    unsigned char p5[] = {1, 100, 100, 100, 100};
    unsigned char z5[] = {1, 0, 0, 0, 0};
    send_one(device, "rid1 one on", 1, p1, sizeof(p1));
    usleep(250000);
    send_one(device, "rid1 one off", 1, z1, sizeof(z1));
    usleep(250000);
    send_one(device, "rid1 raw4 on", 1, p4, sizeof(p4));
    usleep(250000);
    send_one(device, "rid1 raw4 off", 1, z4, sizeof(z4));
    usleep(250000);
    send_one(device, "rid1 id-prefixed on", 1, p5, sizeof(p5));
    usleep(250000);
    send_one(device, "rid1 id-prefixed off", 1, z5, sizeof(z5));
    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
