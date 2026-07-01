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

    const void **values = calloc((size_t)CFSetGetCount(devices), sizeof(void *));
    CFSetGetValues(devices, values);
    IOHIDDeviceRef device = (IOHIDDeviceRef)values[0];
    CFRetain(device);
    free(values);
    CFRelease(devices);
    return device;
}

static void callback(void *context, IOReturn result, void *sender, IOHIDReportType type,
                     uint32_t report_id, uint8_t *report, CFIndex report_len) {
    const char *label = context;
    printf("callback %s => 0x%08x sender=%p type=%d rid=%u len=%ld\n",
           label, result, sender, type, report_id, (long)report_len);
    fflush(stdout);
}

static void send_report(IOHIDDeviceRef device, const char *label, CFIndex report_id,
                        const unsigned char *bytes, size_t len) {
    IOReturn result = IOHIDDeviceSetReportWithCallback(
        device, kIOHIDReportTypeOutput, report_id, bytes, len, 1.0, callback, (void *)label);
    printf("submit %s rid=%ld len=%zu => 0x%08x", label, (long)report_id, len, result);
    for (size_t i = 0; i < len; i++) {
        printf(" %02x", bytes[i]);
    }
    printf("\n");
    fflush(stdout);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, false);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "8BitDo Bluetooth HID device 2dc8:301b not found\n");
        return 1;
    }

    printf("open => 0x%08x\n", IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone));

    const unsigned char raw_on[] = {100, 100, 100, 100};
    const unsigned char raw_off[] = {0, 0, 0, 0};
    const unsigned char id_on[] = {5, 100, 100, 100, 100};
    const unsigned char id_off[] = {5, 0, 0, 0, 0};

    send_report(device, "raw4 on", 5, raw_on, sizeof(raw_on));
    usleep(700000);
    send_report(device, "raw4 off", 5, raw_off, sizeof(raw_off));
    usleep(250000);
    send_report(device, "id5 on", 0, id_on, sizeof(id_on));
    usleep(700000);
    send_report(device, "id5 off", 0, id_off, sizeof(id_off));

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
