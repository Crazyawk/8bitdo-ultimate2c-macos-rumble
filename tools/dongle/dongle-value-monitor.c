#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>

static void set_int(CFMutableDictionaryRef dict, CFStringRef key, int value) {
    CFNumberRef number = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &value);
    CFDictionarySetValue(dict, key, number);
    CFRelease(number);
}

static void value_cb(void *context, IOReturn result, void *sender, IOHIDValueRef value) {
    (void)context; (void)sender;
    IOHIDElementRef element = IOHIDValueGetElement(value);
    printf("value result=0x%08x page=%u usage=%u report=%u cookie=%ld int=%ld\n",
           result,
           IOHIDElementGetUsagePage(element),
           IOHIDElementGetUsage(element),
           IOHIDElementGetReportID(element),
           (long)IOHIDElementGetCookie(element),
           (long)IOHIDValueGetIntegerValue(value));
    fflush(stdout);
}

int main(void) {
    IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
    CFMutableDictionaryRef match = CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    set_int(match, CFSTR(kIOHIDVendorIDKey), 0x2DC8);
    set_int(match, CFSTR(kIOHIDProductIDKey), 0x310A);
    IOHIDManagerSetDeviceMatching(manager, match);
    CFRelease(match);

    IOHIDManagerRegisterInputValueCallback(manager, value_cb, NULL);
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOReturn open_result = IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone);
    printf("open=0x%08x monitoring values for 10 seconds...\n", open_result);
    fflush(stdout);
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 10.0, false);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    return 0;
}
