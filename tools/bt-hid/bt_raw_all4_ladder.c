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
    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    CFSetGetValues(devices, values);
    IOHIDDeviceRef device = NULL;
    for (CFIndex i = 0; i < count; i++) {
        IOHIDDeviceRef candidate = (IOHIDDeviceRef)values[i];
        CFTypeRef transport = IOHIDDeviceGetProperty(candidate, CFSTR(kIOHIDTransportKey));
        CFTypeRef max_output = IOHIDDeviceGetProperty(candidate, CFSTR(kIOHIDMaxOutputReportSizeKey));
        int output_size = 0;
        if (max_output && CFGetTypeID(max_output) == CFNumberGetTypeID()) {
            CFNumberGetValue((CFNumberRef)max_output, kCFNumberIntType, &output_size);
        }
        if (transport && CFGetTypeID(transport) == CFStringGetTypeID() &&
            CFStringCompare((CFStringRef)transport, CFSTR("Bluetooth Low Energy"), 0) == kCFCompareEqualTo &&
            output_size == 5) {
            device = candidate;
            break;
        }
    }
    if (!device) device = (IOHIDDeviceRef)values[0];
    CFRetain(device);
    free(values);
    CFRelease(devices);
    return device;
}

static void pulse(IOHIDDeviceRef device, unsigned char strength) {
    unsigned char payload[4] = {strength, strength, strength, strength};
    unsigned char off[4] = {0, 0, 0, 0};
    IOReturn on = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1, payload, 4);
    printf("all4_raw_%03u on => 0x%08x\n", strength, on);
    fflush(stdout);
    usleep(650000);
    IOReturn stop = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1, off, 4);
    printf("all4_raw_%03u off => 0x%08x\n", strength, stop);
    fflush(stdout);
    usleep(750000);
}

int main(int argc, char **argv) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "BT device not found\n");
        return 1;
    }
    printf("open => 0x%08x\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));

    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            int value = atoi(argv[i]);
            if (value < 0) value = 0;
            if (value > 255) value = 255;
            pulse(device, (unsigned char)value);
        }
    } else {
        pulse(device, 1);
        pulse(device, 8);
        pulse(device, 16);
        pulse(device, 32);
        pulse(device, 64);
        pulse(device, 128);
        pulse(device, 255);
    }

    IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, 1, (unsigned char[]){0, 0, 0, 0}, 4);
    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
