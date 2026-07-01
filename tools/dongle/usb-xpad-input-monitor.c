#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#include <mach/mach.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int int_prop(io_service_t service, CFStringRef key, int fallback) {
    CFTypeRef ref = IORegistryEntryCreateCFProperty(service, key, kCFAllocatorDefault, 0);
    int value = fallback;
    if (ref) {
        CFNumberGetValue((CFNumberRef)ref, kCFNumberIntType, &value);
        CFRelease(ref);
    }
    return value;
}

static io_service_t find_xinput_interface(void) {
    CFMutableDictionaryRef match = IOServiceMatching("IOUSBHostInterface");
    io_iterator_t iter = MACH_PORT_NULL;
    if (!match || IOServiceGetMatchingServices(kIOMainPortDefault, match, &iter) != KERN_SUCCESS) {
        return MACH_PORT_NULL;
    }

    io_service_t service = MACH_PORT_NULL;
    io_service_t candidate = MACH_PORT_NULL;
    while ((candidate = IOIteratorNext(iter))) {
        int vendor = int_prop(candidate, CFSTR("idVendor"), 0);
        int product = int_prop(candidate, CFSTR("idProduct"), 0);
        int iface = int_prop(candidate, CFSTR("bInterfaceNumber"), -1);
        int klass = int_prop(candidate, CFSTR("bInterfaceClass"), -1);
        int subclass = int_prop(candidate, CFSTR("bInterfaceSubClass"), -1);
        int protocol = int_prop(candidate, CFSTR("bInterfaceProtocol"), -1);
        if (vendor == 0x2dc8 && product == 0x310a && iface == 0 &&
            klass == 0xff && subclass == 0x5d && protocol == 1) {
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
        service, kIOUSBInterfaceUserClientTypeID, kIOCFPlugInInterfaceID, &plugin, &score);
    if (kr != KERN_SUCCESS || !plugin) {
        printf("IOCreatePlugInInterfaceForService => 0x%08x\n", kr);
        return NULL;
    }

    IOUSBInterfaceInterface300 **iface = NULL;
    HRESULT hr = (*plugin)->QueryInterface(
        plugin, CFUUIDGetUUIDBytes(kIOUSBInterfaceInterfaceID300), (LPVOID *)&iface);
    (*plugin)->Release(plugin);
    if (hr || !iface) {
        printf("QueryInterface => 0x%08x\n", (unsigned int)hr);
        return NULL;
    }
    return iface;
}

static UInt8 find_in_pipe(IOUSBInterfaceInterface300 **iface) {
    UInt8 pipes = 0;
    (*iface)->GetNumEndpoints(iface, &pipes);
    for (UInt8 pipe = 1; pipe <= pipes; pipe++) {
        UInt8 direction = 0, number = 0, transfer_type = 0, interval = 0;
        UInt16 max_packet = 0;
        IOReturn r = (*iface)->GetPipeProperties(
            iface, pipe, &direction, &number, &transfer_type, &max_packet, &interval);
        printf("pipe=%u props=>0x%08x dir=%u endpoint=%u type=%u max=%u interval=%u\n",
               pipe, r, direction, number, transfer_type, max_packet, interval);
        if (r == kIOReturnSuccess && direction == kUSBIn && transfer_type == kUSBInterrupt) {
            return pipe;
        }
    }
    return 0;
}

int main(void) {
    io_service_t service = find_xinput_interface();
    if (!service) {
        fprintf(stderr, "XInput interface 0 for 2dc8:310a not found\n");
        return 1;
    }

    IOUSBInterfaceInterface300 **iface = make_interface(service);
    IOObjectRelease(service);
    if (!iface) return 1;

    IOReturn open = (*iface)->USBInterfaceOpen(iface);
    printf("USBInterfaceOpen => 0x%08x\n", open);
    if (open != kIOReturnSuccess) {
        (*iface)->Release(iface);
        return 1;
    }

    UInt8 in_pipe = find_in_pipe(iface);
    if (!in_pipe) {
        fprintf(stderr, "No interrupt IN pipe found\n");
        (*iface)->USBInterfaceClose(iface);
        (*iface)->Release(iface);
        return 1;
    }

    printf("reading input pipe %u for 10 seconds; press buttons/move sticks now...\n", in_pipe);
    fflush(stdout);
    for (int i = 0; i < 100; i++) {
        UInt8 buffer[64] = {0};
        UInt32 size = sizeof(buffer);
        IOReturn r = (*iface)->ReadPipeTO(iface, in_pipe, buffer, &size, 100, 100);
        if (r == kIOReturnSuccess) {
            printf("in len=%u", size);
            for (UInt32 j = 0; j < size && j < 32; j++) printf(" %02x", buffer[j]);
            printf("\n");
            fflush(stdout);
        } else if (r != kIOReturnTimeout) {
            printf("ReadPipeTO => 0x%08x\n", r);
            fflush(stdout);
            usleep(100000);
        }
    }

    (*iface)->USBInterfaceClose(iface);
    (*iface)->Release(iface);
    return 0;
}
