#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef int (*rumble_fn)(void *, uint16_t, uint16_t, uint32_t);
typedef int (*trigger_fn)(void *, uint16_t, uint16_t, uint32_t);

int main(void) {
    void *sdl = dlopen("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/libSDL2-2.0.0.dylib",
                       RTLD_NOW | RTLD_LOCAL);
    if (!sdl) {
        fprintf(stderr, "dlopen SDL shim failed: %s\n", dlerror());
        return 1;
    }

    rumble_fn rumble = (rumble_fn)dlsym(sdl, "SDL_JoystickRumble");
    trigger_fn triggers = (trigger_fn)dlsym(sdl, "SDL_JoystickRumbleTriggers");
    if (!rumble || !triggers) {
        fprintf(stderr, "dlsym rumble failed: %s\n", dlerror());
        return 1;
    }

    printf("GTA-style test: main rumble raw=[110 0], zero trigger, short normal stop\n");
    fflush(stdout);
    rumble(NULL, 0x6e00, 0x0000, 1000);
    triggers(NULL, 0x0000, 0x0000, 1000);
    usleep(80000);
    rumble(NULL, 0x0000, 0x0000, 0);
    triggers(NULL, 0x0000, 0x0000, 0);
    usleep(500000);

    printf("GTA-style test: high rumble raw=[0 105], zero trigger, short normal stop\n");
    fflush(stdout);
    rumble(NULL, 0x0000, 0x6900, 1000);
    triggers(NULL, 0x0000, 0x0000, 1000);
    usleep(80000);
    rumble(NULL, 0x0000, 0x0000, 0);
    triggers(NULL, 0x0000, 0x0000, 0);
    usleep(500000);
    return 0;
}
