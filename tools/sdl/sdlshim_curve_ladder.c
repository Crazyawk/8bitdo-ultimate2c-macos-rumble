#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

typedef int (*rumble_fn)(void *, uint16_t, uint16_t, uint32_t);

static void pulse(rumble_fn rumble, const char *label, uint16_t strength) {
    printf("%s SDL strength=%u\n", label, strength);
    fflush(stdout);
    rumble(NULL, strength, strength, 650);
    usleep(850000);
    rumble(NULL, 0, 0, 0);
    usleep(450000);
}

int main(void) {
    void *sdl = dlopen("/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/libSDL2-2.0.0.dylib",
                       RTLD_NOW | RTLD_LOCAL);
    if (!sdl) {
        fprintf(stderr, "dlopen SDL shim failed: %s\n", dlerror());
        return 1;
    }
    rumble_fn rumble = (rumble_fn)dlsym(sdl, "SDL_GameControllerRumble");
    if (!rumble) {
        fprintf(stderr, "dlsym SDL_GameControllerRumble failed: %s\n", dlerror());
        return 1;
    }

    pulse(rumble, "01_texture_low_out24", 0x1000);
    pulse(rumble, "02_texture_mid_out35", 0x3000);
    pulse(rumble, "03_texture_high_out50", 0x6000);
    pulse(rumble, "04_texture_top_out56", 0x9000);
    pulse(rumble, "05_texture_cap_out60", 0xc000);
    pulse(rumble, "06_pre_impact_out60", 0xef00);
    pulse(rumble, "07_impact_overdrive_out255", 0xf000);
    pulse(rumble, "08_full_overdrive_out255", 0xffff);
    return 0;
}
