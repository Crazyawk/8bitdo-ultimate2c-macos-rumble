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

static void try_report(IOHIDDeviceRef device, const char *label, CFIndex report_id,
                       const unsigned char *on, size_t on_len,
                       const unsigned char *off, size_t off_len) {
    IOReturn on_result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, report_id, on, on_len);
    usleep(250000);
    IOReturn off_result = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, report_id, off, off_len);
    printf("%s rid=%ld on=0x%08x off=0x%08x\n", label, (long)report_id, on_result, off_result);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "device not found\n");
        return 1;
    }

    IOReturn open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeSeizeDevice);
    printf("open seize => 0x%08x\n", open_result);
    if (open_result != kIOReturnSuccess) {
        IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
        CFRelease(device);
        CFRelease(manager);
        return 2;
    }

    const unsigned char raw4_on[] = {80, 80, 80, 80};
    const unsigned char raw4_off[] = {0, 0, 0, 0};
    const unsigned char id5_on[] = {5, 80, 80, 80, 80};
    const unsigned char id5_off[] = {5, 0, 0, 0, 0};

    try_report(device, "raw4", 5, raw4_on, sizeof(raw4_on), raw4_off, sizeof(raw4_off));
    usleep(250000);
    try_report(device, "id5", 0, id5_on, sizeof(id5_on), id5_off, sizeof(id5_off));

    IOHIDDeviceClose(device, kIOHIDOptionsTypeSeizeDevice);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
