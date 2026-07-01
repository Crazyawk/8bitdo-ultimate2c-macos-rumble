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

static IOHIDDeviceRef find_device(IOHIDManagerRef manager) {
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2DC8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x310A);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
        return NULL;
    }

    CFSetRef devices = IOHIDManagerCopyDevices(manager);
    if (!devices) return NULL;

    CFIndex count = CFSetGetCount(devices);
    const void **values = calloc((size_t)count, sizeof(void *));
    CFSetGetValues(devices, values);

    IOHIDDeviceRef found = NULL;
    for (CFIndex i = 0; i < count; i++) {
        IOHIDDeviceRef device = (IOHIDDeviceRef)values[i];
        CFTypeRef usage_page_ref = IOHIDDeviceGetProperty(device, CFSTR(kIOHIDPrimaryUsagePageKey));
        int usage_page = 0;
        if (usage_page_ref) {
            CFNumberGetValue((CFNumberRef)usage_page_ref, kCFNumberIntType, &usage_page);
        }
        if (usage_page == 0xFF7A) {
            found = device;
            CFRetain(found);
            break;
        }
    }

    free(values);
    CFRelease(devices);
    return found;
}

static void send_packet(IOHIDDeviceRef device, const char *label, IOHIDReportType type,
                        CFIndex report_id, int include_id, int fixed_64,
                        const unsigned char *packet, size_t len) {
    unsigned char report[64] = {0};
    size_t offset = include_id ? 1 : 0;
    if (include_id) report[0] = (unsigned char)report_id;
    memcpy(report + offset, packet, len);

    size_t send_len = fixed_64 ? sizeof(report) : len + offset;
    IOReturn result = IOHIDDeviceSetReport(device, type, report_id, report, send_len);
    printf("%s type=%s id=%ld len=%zu => 0x%08x",
        label, type == kIOHIDReportTypeFeature ? "feature" : "output",
        (long)report_id, send_len, result);
    for (size_t i = 0; i < send_len && i < 14; i++) printf(" %02x", report[i]);
    printf("\n");
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    IOHIDDeviceRef device = find_device(manager);
    if (!device) {
        fprintf(stderr, "USB vendor HID interface for 8BitDo 2dc8:310a not found\n");
        CFRelease(manager);
        return 1;
    }

    IOReturn open_result = IOHIDDeviceOpen(device, kIOHIDOptionsTypeNone);
    printf("open => 0x%08x\n", open_result);

    const unsigned char xbox360_on[] = {0x00, 0x08, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00};
    const unsigned char xbox360_off[] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const unsigned char xboxone_on[] = {0x09, 0x00, 0x0f, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00};
    const unsigned char xboxone_off[] = {0x09, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00};
    const unsigned char xboxone_alt_on[] = {0x09, 0x08, 0x00, 0x09, 0x00, 0xff, 0xff, 0xff, 0xff, 0x00};
    const unsigned char xboxone_alt_off[] = {0x09, 0x08, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const unsigned char xpadone_on[] = {0x09, 0x00, 0x01, 0x09, 0x00, 0x0f, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff};
    const unsigned char xpadone_off[] = {0x09, 0x00, 0x02, 0x09, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0xff, 0x00, 0x00};

    send_packet(device, "360 full id", kIOHIDReportTypeOutput, 0x81, 1, 1, xbox360_on, sizeof(xbox360_on));
    usleep(600 * 1000);
    send_packet(device, "360 full id off", kIOHIDReportTypeOutput, 0x81, 1, 1, xbox360_off, sizeof(xbox360_off));
    usleep(300 * 1000);
    send_packet(device, "360 short noid", kIOHIDReportTypeOutput, 0x81, 0, 0, xbox360_on, sizeof(xbox360_on));
    usleep(600 * 1000);
    send_packet(device, "360 short noid off", kIOHIDReportTypeOutput, 0x81, 0, 0, xbox360_off, sizeof(xbox360_off));
    usleep(300 * 1000);
    send_packet(device, "xbone full id", kIOHIDReportTypeOutput, 0x81, 1, 1, xboxone_on, sizeof(xboxone_on));
    usleep(600 * 1000);
    send_packet(device, "xbone full id off", kIOHIDReportTypeOutput, 0x81, 1, 1, xboxone_off, sizeof(xboxone_off));
    usleep(300 * 1000);
    send_packet(device, "xbone short noid", kIOHIDReportTypeOutput, 0x81, 0, 0, xboxone_on, sizeof(xboxone_on));
    usleep(600 * 1000);
    send_packet(device, "xbone short noid off", kIOHIDReportTypeOutput, 0x81, 0, 0, xboxone_off, sizeof(xboxone_off));
    usleep(300 * 1000);
    send_packet(device, "xbone alt short", kIOHIDReportTypeOutput, 0x81, 0, 0, xboxone_alt_on, sizeof(xboxone_alt_on));
    usleep(600 * 1000);
    send_packet(device, "xbone alt short off", kIOHIDReportTypeOutput, 0x81, 0, 0, xboxone_alt_off, sizeof(xboxone_alt_off));
    usleep(300 * 1000);
    send_packet(device, "360 feature", kIOHIDReportTypeFeature, 0x81, 1, 1, xbox360_on, sizeof(xbox360_on));
    usleep(600 * 1000);
    send_packet(device, "360 feature off", kIOHIDReportTypeFeature, 0x81, 1, 1, xbox360_off, sizeof(xbox360_off));
    usleep(300 * 1000);
    send_packet(device, "xpadone exact full", kIOHIDReportTypeOutput, 0x81, 1, 1, xpadone_on, sizeof(xpadone_on));
    usleep(900 * 1000);
    send_packet(device, "xpadone exact full off", kIOHIDReportTypeOutput, 0x81, 1, 1, xpadone_off, sizeof(xpadone_off));
    usleep(300 * 1000);
    send_packet(device, "xpadone exact short", kIOHIDReportTypeOutput, 0x81, 0, 0, xpadone_on, sizeof(xpadone_on));
    usleep(900 * 1000);
    send_packet(device, "xpadone exact short off", kIOHIDReportTypeOutput, 0x81, 0, 0, xpadone_off, sizeof(xpadone_off));

    IOHIDDeviceClose(device, kIOHIDOptionsTypeNone);
    CFRelease(device);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
