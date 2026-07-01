#import <Foundation/Foundation.h>
#import <GameController/GameController.h>
#import <CoreHaptics/CoreHaptics.h>

static void pump_runloop(NSTimeInterval seconds) {
    NSDate *until = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while ([until timeIntervalSinceNow] > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
    }
}

int main(void) {
    @autoreleasepool {
        printf("Starting wireless discovery...\n");
        [GCController startWirelessControllerDiscoveryWithCompletionHandler:^{
            printf("Discovery completion handler fired.\n");
        }];
        pump_runloop(2.0);

        NSArray<GCController *> *controllers = [GCController controllers];
        printf("controllers=%lu\n", (unsigned long)controllers.count);
        for (GCController *controller in controllers) {
            printf("controller vendor=%s product=%s physical=%d attached=%d\n",
                   controller.vendorName.UTF8String ?: "(null)",
                   controller.productCategory.UTF8String ?: "(null)",
                   controller.isAttachedToDevice,
                   controller.isAttachedToDevice);
            printf("  haptics=%s\n", controller.haptics ? "yes" : "no");
            if (controller.haptics) {
                printf("  localities:");
                for (GCHapticsLocality locality in controller.haptics.supportedLocalities) {
                    printf(" %s", locality.UTF8String);
                }
                printf("\n");
            }
        }

        GCController *target = nil;
        for (GCController *controller in controllers) {
            if ([controller.vendorName localizedCaseInsensitiveContainsString:@"8BitDo"] ||
                [controller.productCategory localizedCaseInsensitiveContainsString:@"8BitDo"]) {
                target = controller;
                break;
            }
        }
        if (!target && controllers.count > 0) {
            target = controllers.firstObject;
        }
        if (!target) {
            fprintf(stderr, "No GameController device found.\n");
            return 1;
        }
        if (!target.haptics) {
            fprintf(stderr, "Selected controller has no GameController haptics object.\n");
            return 2;
        }

        NSError *error = nil;
        CHHapticEngine *engine = [target.haptics createEngineWithLocality:GCHapticsLocalityDefault];
        if (!engine) {
            fprintf(stderr, "createEngineWithLocality returned nil.\n");
            return 3;
        }
        [engine startAndReturnError:&error];
        if (error) {
            fprintf(stderr, "engine start failed: %s\n", error.localizedDescription.UTF8String);
            return 4;
        }

        CHHapticEventParameter *intensity =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                          value:1.0f];
        CHHapticEventParameter *sharpness =
            [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                          value:0.5f];
        CHHapticEvent *event =
            [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                         parameters:@[intensity, sharpness]
                                       relativeTime:0.0
                                           duration:0.7];
        CHHapticPattern *pattern = [[CHHapticPattern alloc] initWithEvents:@[event]
                                                            parameterCurves:@[]
                                                                     error:&error];
        if (error) {
            fprintf(stderr, "pattern failed: %s\n", error.localizedDescription.UTF8String);
            return 5;
        }
        id<CHHapticPatternPlayer> player = [engine createPlayerWithPattern:pattern error:&error];
        if (error) {
            fprintf(stderr, "player failed: %s\n", error.localizedDescription.UTF8String);
            return 6;
        }
        printf("Playing haptic pattern now...\n");
        [player startAtTime:CHHapticTimeImmediate error:&error];
        if (error) {
            fprintf(stderr, "play failed: %s\n", error.localizedDescription.UTF8String);
            return 7;
        }
        pump_runloop(1.0);
        [engine stopWithCompletionHandler:nil];
        printf("Done.\n");
    }
    return 0;
}
