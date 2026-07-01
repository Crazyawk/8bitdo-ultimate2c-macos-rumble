#include <CoreFoundation/CoreFoundation.h>
#include <objc/message.h>
#include <objc/runtime.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern id GCHapticsLocalityDefault;
extern id CHHapticEventTypeHapticContinuous;
extern id CHHapticEventParameterIDHapticIntensity;
extern id CHHapticEventParameterIDHapticSharpness;

static SEL sel(const char *name) {
    return sel_registerName(name);
}

static id msg_id(id receiver, const char *name) {
    return ((id (*)(id, SEL))objc_msgSend)(receiver, sel(name));
}

static id msg_id_id(id receiver, const char *name, id arg) {
    return ((id (*)(id, SEL, id))objc_msgSend)(receiver, sel(name), arg);
}

static unsigned long msg_ulong(id receiver, const char *name) {
    return ((unsigned long (*)(id, SEL))objc_msgSend)(receiver, sel(name));
}

static bool msg_bool(id receiver, const char *name) {
    return ((bool (*)(id, SEL))objc_msgSend)(receiver, sel(name));
}

static void print_string(id string) {
    if (!string) {
        printf("(null)");
        return;
    }
    char buffer[512];
    if (CFStringGetCString((CFStringRef)string, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
        printf("%s", buffer);
    } else {
        printf("(unprintable)");
    }
}

static id nsarray_from(id *objects, unsigned long count) {
    id nsarray = (id)objc_getClass("NSArray");
    return ((id (*)(id, SEL, const id *, unsigned long))objc_msgSend)(
        nsarray, sel("arrayWithObjects:count:"), objects, count);
}

static void pump_runloop(double seconds) {
    CFAbsoluteTime end = CFAbsoluteTimeGetCurrent() + seconds;
    while (CFAbsoluteTimeGetCurrent() < end) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    }
}

int main(void) {
    freopen("/tmp/gc_haptics_dyn_app.log", "a", stdout);
    freopen("/tmp/gc_haptics_dyn_app.log", "a", stderr);
    printf("=== gc_haptics_probe_dyn ===\n");
    fflush(stdout);

    id gc_controller = (id)objc_getClass("GCController");
    if (!gc_controller) {
        fprintf(stderr, "GCController class not found.\n");
        return 1;
    }

    printf("Starting wireless discovery...\n");
    ((void (*)(id, SEL, id))objc_msgSend)(
        gc_controller, sel("startWirelessControllerDiscoveryWithCompletionHandler:"), nil);
    pump_runloop(2.0);

    id controllers = msg_id(gc_controller, "controllers");
    unsigned long count = msg_ulong(controllers, "count");
    printf("controllers=%lu\n", count);

    id target = nil;
    for (unsigned long i = 0; i < count; i++) {
        id controller = ((id (*)(id, SEL, unsigned long))objc_msgSend)(
            controllers, sel("objectAtIndex:"), i);
        id vendor = msg_id(controller, "vendorName");
        id product = msg_id(controller, "productCategory");
        bool attached = msg_bool(controller, "isAttachedToDevice");
        id haptics = msg_id(controller, "haptics");

        printf("controller[%lu] vendor=", i);
        print_string(vendor);
        printf(" product=");
        print_string(product);
        printf(" attached=%d haptics=%s\n", attached ? 1 : 0, haptics ? "yes" : "no");

        if (haptics) {
            id localities = msg_id(haptics, "supportedLocalities");
            unsigned long locality_count = msg_ulong(localities, "count");
            printf("  localities count=%lu\n", locality_count);
        }
        if (!target && haptics) {
            target = controller;
        }
    }

    if (!target) {
        fprintf(stderr, "No haptics-capable GameController found.\n");
        return 2;
    }

    id haptics = msg_id(target, "haptics");
    id engine = msg_id_id(haptics, "createEngineWithLocality:", GCHapticsLocalityDefault);
    if (!engine) {
        fprintf(stderr, "createEngineWithLocality returned nil.\n");
        return 3;
    }

    void *error = NULL;
    bool started = ((bool (*)(id, SEL, void **))objc_msgSend)(
        engine, sel("startAndReturnError:"), &error);
    printf("engine start=%d error=%p\n", started ? 1 : 0, error);
    if (!started || error) {
        return 4;
    }

    id event_param_cls = (id)objc_getClass("CHHapticEventParameter");
    id intensity = ((id (*)(id, SEL, id, float))objc_msgSend)(
        msg_id(event_param_cls, "alloc"),
        sel("initWithParameterID:value:"),
        CHHapticEventParameterIDHapticIntensity,
        1.0f);
    id sharpness = ((id (*)(id, SEL, id, float))objc_msgSend)(
        msg_id(event_param_cls, "alloc"),
        sel("initWithParameterID:value:"),
        CHHapticEventParameterIDHapticSharpness,
        0.5f);
    id params_objs[2] = { intensity, sharpness };
    id params = nsarray_from(params_objs, 2);

    id event_cls = (id)objc_getClass("CHHapticEvent");
    id event = ((id (*)(id, SEL, id, id, double, double))objc_msgSend)(
        msg_id(event_cls, "alloc"),
        sel("initWithEventType:parameters:relativeTime:duration:"),
        CHHapticEventTypeHapticContinuous,
        params,
        0.0,
        0.7);
    id event_objs[1] = { event };
    id events = nsarray_from(event_objs, 1);
    id empty = nsarray_from(NULL, 0);

    id pattern_cls = (id)objc_getClass("CHHapticPattern");
    error = NULL;
    id pattern = ((id (*)(id, SEL, id, id, void **))objc_msgSend)(
        msg_id(pattern_cls, "alloc"),
        sel("initWithEvents:parameters:error:"),
        events,
        empty,
        &error);
    printf("pattern=%p error=%p\n", pattern, error);
    if (!pattern || error) {
        return 5;
    }

    error = NULL;
    id player = ((id (*)(id, SEL, id, void **))objc_msgSend)(
        engine, sel("createPlayerWithPattern:error:"), pattern, &error);
    printf("player=%p error=%p\n", player, error);
    if (!player || error) {
        return 6;
    }

    printf("Playing haptic pattern now...\n");
    error = NULL;
    ((bool (*)(id, SEL, double, void **))objc_msgSend)(
        player, sel("startAtTime:error:"), 0.0, &error);
    printf("play error=%p\n", error);
    pump_runloop(1.0);
    ((void (*)(id, SEL, id))objc_msgSend)(engine, sel("stopWithCompletionHandler:"), nil);
    printf("Done.\n");
    return error ? 7 : 0;
}
