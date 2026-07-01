#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static io_service_t find_interface(void) {
    CFMutableDictionaryRef match = IOServiceMatching("IOUSBHostInterface");
    if (!match) return MACH_PORT_NULL;
    io_iterator_t iter = MACH_PORT_NULL;
    kern_return_t kr = IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter);
    if (kr != KERN_SUCCESS) return MACH_PORT_NULL;
    io_service_t service = MACH_PORT_NULL;
    io_service_t candidate = MACH_PORT_NULL;
    while ((candidate = IOIteratorNext(iter))) {
        CFTypeRef vendor_ref = IORegistryEntryCreateCFProperty(candidate, CFSTR("idVendor"), kCFAllocatorDefault, 0);
        CFTypeRef product_ref = IORegistryEntryCreateCFProperty(candidate, CFSTR("idProduct"), kCFAllocatorDefault, 0);
        CFTypeRef iface_ref = IORegistryEntryCreateCFProperty(candidate, CFSTR("bInterfaceNumber"), kCFAllocatorDefault, 0);
        int vendor = 0, product = 0, iface = -1;
        if (vendor_ref) CFNumberGetValue((CFNumberRef)vendor_ref, kCFNumberIntType, &vendor);
        if (product_ref) CFNumberGetValue((CFNumberRef)product_ref, kCFNumberIntType, &product);
        if (iface_ref) CFNumberGetValue((CFNumberRef)iface_ref, kCFNumberIntType, &iface);
        if (vendor_ref) CFRelease(vendor_ref);
        if (product_ref) CFRelease(product_ref);
        if (iface_ref) CFRelease(iface_ref);
        if (vendor == 0x2dc8 && product == 0x310a && iface == 0) {
            service = candidate;
            break;
        }
        IOObjectRelease(candidate);
    }
    IOObjectRelease(iter);
    return service;
}

static IOUSBInterfaceInterface300 **make_interface(io_service_t service) {
    IOCFPlugInInterface **plugin = NULL;
    SInt32 score = 0;
    kern_return_t kr = IOCreatePlugInInterfaceForService(
        service,
        kIOUSBInterfaceUserClientTypeID,
        kIOCFPlugInInterfaceID,
        &plugin,
        &score);
    if (kr != KERN_SUCCESS || !plugin) {
        printf("IOCreatePlugInInterfaceForService => 0x%08x plugin=%p\n", kr, (void *)plugin);
        return NULL;
    }

    IOUSBInterfaceInterface300 **iface = NULL;
    HRESULT hr = (*plugin)->QueryInterface(
        plugin,
        CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300),
        (LPVOID *)&iface);
    (*plugin)->Release(plugin);
    if (hr || !iface) {
        printf("QueryInterface => 0x%08x iface=%p\n", (unsigned int)hr, (void *)iface);
        return NULL;
    }
    return iface;
}

static void dump_pipes(IOUSBInterfaceInterface300 **iface, UInt8 *out_pipe) {
    UInt8 pipes = 0;
    IOReturn r = (*iface)->GetNumEndpoints(iface, &pipes);
    printf("GetNumEndpoints => 0x%08x count=%u\n", r, pipes);
    for (UInt8 pipe = 1; pipe <= pipes; pipe++) {
        UInt8 direction = 0, number = 0, transfer_type = 0, interval = 0;
        UInt16 max_packet = 0;
        r = (*iface)->GetPipeProperties(iface, pipe, &direction, &number,
                                        &transfer_type, &max_packet, &interval);
        printf("pipe=%u props=>0x%08x dir=%u endpoint=%u type=%u max=%u interval=%u\n",
               pipe, r, direction, number, transfer_type, max_packet, interval);
        if (r == kIOReturnSuccess &&
            direction == kUSBOut &&
            transfer_type == kUSBInterrupt) {
            *out_pipe = pipe;
        }
    }
}

static IOReturn write_packet(IOUSBInterfaceInterface300 **iface, UInt8 pipe,
                             const UInt8 *packet, UInt32 len, const char *label) {
    printf("%s pipe=%u len=%u data:", label, pipe, len);
    for (UInt32 i = 0; i < len; i++) printf(" %02x", packet[i]);
    fflush(stdout);
    IOReturn r = (*iface)->WritePipe(iface, pipe, (void *)packet, len);
    printf(" => 0x%08x\n", r);
    fflush(stdout);
    return r;
}

int main(void) {
    io_service_t service = find_interface();
    if (!service) {
        fprintf(stderr, "USB interface 0 for 2dc8:310a not found\n");
        return 1;
    }

    IOUSBInterfaceInterface300 **iface = make_interface(service);
    IOObjectRelease(service);
    if (!iface) return 1;

    IOReturn open = (*iface)->USBInterfaceOpen(iface);
    printf("USBInterfaceOpen => 0x%08x\n", open);
    if (open != kIOReturnSuccess) {
        open = (*iface)->USBInterfaceOpenSeize(iface);
        printf("USBInterfaceOpenSeize => 0x%08x\n", open);
    }
    if (open != kIOReturnSuccess) {
        (*iface)->Release(iface);
        return 1;
    }

    UInt8 out_pipe = 0;
    dump_pipes(iface, &out_pipe);
    if (!out_pipe) {
        fprintf(stderr, "No interrupt OUT pipe found\n");
        (*iface)->USBInterfaceClose(iface);
        (*iface)->Release(iface);
        return 1;
    }

    const UInt8 rumble_on[] = {0x00, 0x08, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00};
    const UInt8 rumble_off[] = {0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    write_packet(iface, out_pipe, rumble_on, sizeof(rumble_on), "xpad-on");
    usleep(900 * 1000);
    write_packet(iface, out_pipe, rumble_off, sizeof(rumble_off), "xpad-off");

    (*iface)->USBInterfaceClose(iface);
    (*iface)->Release(iface);
    return 0;
}
