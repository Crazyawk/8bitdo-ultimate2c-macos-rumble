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

static IOHIDDeviceRef find_bt_device(IOHIDManagerRef manager) {
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2DC8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x301B);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) return NULL;
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices) return NULL;
    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    CFSetGetValues(devices, values);
    IOHIDDeviceRef found = count > 0 ? (IOHIDDeviceRef)values[0] : NULL;
    if (found) CFRetain(found);
    free(values);
    CFRelease(devices);
    return found;
}

static void send_one(IOHIDDeviceRef device, const char *label, IOHIDReportType type,
                     CFIndex report_id, const unsigned char *payload, size_t len) {
    IOReturn result = IOHIDDeviceSetReport(device, type, report_id, payload, len);
    printf("%s type=%s rid=%ld len=%zu => 0x%08x bytes:",
        label,
        type == kIOHIDReportTypeFeature ? "feature" : "output",
        (long)report_id, len, result);
    for (size_t i = 0; i < len; i++) printf(" %02x", payload[i]);
    printf("\n");
    fflush(stdout);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_bt_device(manager);
    if (!device) {
        fprintf(stderr, "Bluetooth 8BitDo 2dc8:301b HID device not found\n");
        return 1;
    }
    IOReturn open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
    printf("open => 0x%08x\n", open_result);

    const unsigned char on4_100[] = {100, 100, 100, 100};
    const unsigned char off4[] = {0, 0, 0, 0};
    const unsigned char on5_id100[] = {5, 100, 100, 100, 100};
    const unsigned char off5_id[] = {5, 0, 0, 0, 0};
    const unsigned char on5_noid[] = {100, 100, 100, 100, 0};
    const unsigned char off5_noid[] = {0, 0, 0, 0, 0};
    const unsigned char on1[] = {100};
    const unsigned char off1[] = {0};

    send_one(device, "output raw4 on", kIOHIDReportTypeOutput, 5, on4_100, sizeof(on4_100));
    usleep(800000);
    send_one(device, "output raw4 off", kIOHIDReportTypeOutput, 5, off4, sizeof(off4));
    usleep(300000);
    send_one(device, "output id5 on", kIOHIDReportTypeOutput, 5, on5_id100, sizeof(on5_id100));
    usleep(800000);
    send_one(device, "output id5 off", kIOHIDReportTypeOutput, 5, off5_id, sizeof(off5_id));
    usleep(300000);
    send_one(device, "output noid5 on", kIOHIDReportTypeOutput, 5, on5_noid, sizeof(on5_noid));
    usleep(800000);
    send_one(device, "output noid5 off", kIOHIDReportTypeOutput, 5, off5_noid, sizeof(off5_noid));
    usleep(300000);
    send_one(device, "output rid0 id5 on", kIOHIDReportTypeOutput, 0, on5_id100, sizeof(on5_id100));
    usleep(800000);
    send_one(device, "output rid0 id5 off", kIOHIDReportTypeOutput, 0, off5_id, sizeof(off5_id));
    usleep(300000);
    send_one(device, "feature id5 on", kIOHIDReportTypeFeature, 5, on5_id100, sizeof(on5_id100));
    usleep(800000);
    send_one(device, "feature id5 off", kIOHIDReportTypeFeature, 5, off5_id, sizeof(off5_id));
    usleep(300000);
    send_one(device, "output rid1 one on", kIOHIDReportTypeOutput, 1, on1, sizeof(on1));
    usleep(500000);
    send_one(device, "output rid1 one off", kIOHIDReportTypeOutput, 1, off1, sizeof(off1));

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
