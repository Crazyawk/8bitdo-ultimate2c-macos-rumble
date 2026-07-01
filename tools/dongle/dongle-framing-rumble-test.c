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

static int get_int(IOHIDDeviceRef device, CFStringRef key) {
    CFTypeRef ref = IOHIDDeviceGetProperty(device, key);
    int value = 0;
    if (ref) CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &value);
    return value;
}

static IOHIDDeviceRef find_vendor_interface(IOHIDManagerRef manager) {
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2dc8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x310a);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) return NULL;
    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices) return NULL;

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    CFSetGetValues(devices, values);

    IOHIDDeviceRef found = NULL;
    for (CFIndex i = 0; i < count; i++) {
        IOHIDDeviceRef device = (IOHIDDeviceRef)values[i];
        if (get_int(device, CFSTR(kIOHIDPrimaryUsagePageKey)) == 0xff7a &&
            get_int(device, CFSTR(kIOHIDPrimaryUsageKey)) == 1) {
            found = device;
            CFRetain(found);
            break;
        }
    }

    free(values);
    CFRelease(devices);
    return found;
}

static void print_bytes(const unsigned char *data, size_t len) {
    size_t shown = len < 12 ? len : 12;
    for (size_t i = 0; i < shown; i++) printf(" %02x", data[i]);
}

static void send_case(IOHIDDeviceRef device, const char *label, CFIndex report_id,
                      const unsigned char *on, size_t on_len,
                      const unsigned char *off, size_t off_len) {
    printf("%s on id=0x%lx len=%zu data:", label, (long)report_id, on_len);
    print_bytes(on, on_len);
    IOReturn ron = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, report_id, on, on_len);
    printf(" => 0x%08x\n", ron);
    fflush(stdout);
    usleep(900 * 1000);

    printf("%s off id=0x%lx len=%zu data:", label, (long)report_id, off_len);
    print_bytes(off, off_len);
    IOReturn roff = IOHIDDeviceSetReport(device, kIOHIDReportTypeOutput, report_id, off, off_len);
    printf(" => 0x%08x\n", roff);
    fflush(stdout);
    usleep(700 * 1000);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_vendor_interface(manager);
    if (!device) {
        fprintf(stderr, "8BitDo 2dc8:310a vendor HID interface not found\n");
        CFRelease(manager);
        return 1;
    }

    IOReturn open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
    printf("open=0x%08x\n", open_result);
    if (open_result != kIOReturnSuccess) return 1;

    unsigned char payload_on[63] = {0x05, 0xff, 0xff, 0x00, 0x00};
    unsigned char payload_off[63] = {0x05, 0x00, 0x00, 0x00, 0x00};
    unsigned char full_on[64] = {0x81, 0x05, 0xff, 0xff, 0x00, 0x00};
    unsigned char full_off[64] = {0x81, 0x05, 0x00, 0x00, 0x00, 0x00};

    send_case(device, "A payload-only", 0x81, payload_on, sizeof(payload_on), payload_off, sizeof(payload_off));
    send_case(device, "B full-buffer/report-81", 0x81, full_on, sizeof(full_on), full_off, sizeof(full_off));
    send_case(device, "C full-buffer/report-0", 0x00, full_on, sizeof(full_on), full_off, sizeof(full_off));

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
