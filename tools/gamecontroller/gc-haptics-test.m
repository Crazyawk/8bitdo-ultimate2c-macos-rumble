#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>

static void printController(GCController *controller, NSUInteger index) {
    printf("controller[%lu]: %s\n", (unsigned long)index,
           controller.vendorName.UTF8String ?: "(unknown)");
    printf("  extendedGamepad=%s microGamepad=%s physicalInput=%s\n",
           controller.extendedGamepad ? "yes" : "no",
           controller.microGamepad ? "yes" : "no",
           controller.physicalInputProfile ? "yes" : "no");

    GCDeviceHaptics *haptics = controller.haptics;
    printf("  haptics=%s\n", haptics ? "yes" : "no");
    if (haptics) {
        printf("  localities:");
        for (NSString *locality in haptics.supportedLocalities) {
            printf(" %s", locality.UTF8String);
        }
        printf("\n");
    }
}

int main(void) {
    @autoreleasepool {
        [GCController startWirelessControllerDiscoveryWithCompletionHandler:nil];
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:2.0]];

        NSArray<GCController *> *controllers = [GCController controllers];
        printf("GameController controllers=%lu\n", (unsigned long)controllers.count);
        for (NSUInteger i = 0; i < controllers.count; i++) {
            printController(controllers[i], i);
        }

        if (controllers.count == 0) {
            return 2;
        }

        GCController *controller = controllers[0];
        GCDeviceHaptics *haptics = controller.haptics;
        if (!haptics || haptics.supportedLocalities.count == 0) {
            printf("No GameController haptics localities exposed.\n");
            return 3;
        }

        NSString *locality = haptics.supportedLocalities[0];
        NSError *error = nil;
        CHHapticEngine *engine = [haptics createEngineWithLocality:locality];
        if (!engine) {
            printf("Failed to create haptic engine.\n");
            return 4;
        }

        if (![engine startAndReturnError:&error]) {
            printf("Failed to start haptic engine: %s\n",
                   error.localizedDescription.UTF8String ?: "(unknown)");
            return 5;
        }

        CHHapticEventParameter *intensity =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                          value:1.0f];
        CHHapticEventParameter *sharpness =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                          value:0.5f];
        CHHapticEvent *event =
            [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                          parameters:@[ intensity, sharpness ]
                                        relativeTime:0.0
                                            duration:1.0];
        CHHapticPattern *pattern =
            [[CHHapticPattern alloc] initWithEvents:@[ event ] parameters:@[] error:&error];
        if (!pattern) {
            printf("Failed to create haptic pattern: %s\n",
                   error.localizedDescription.UTF8String ?: "(unknown)");
            return 6;
        }

        id<CHHapticPatternPlayer> player = [engine createPlayerWithPattern:pattern error:&error];
        if (!player) {
            printf("Failed to create haptic player: %s\n",
                   error.localizedDescription.UTF8String ?: "(unknown)");
            return 7;
        }

        printf("Playing 1s haptic pattern on locality %s...\n", locality.UTF8String);
        if (![player startAtTime:0 error:&error]) {
            printf("Failed to play haptic pattern: %s\n",
                   error.localizedDescription.UTF8String ?: "(unknown)");
            return 8;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:1.5]];
        [engine stopWithCompletionHandler:nil];
    }
    return 0;
}
