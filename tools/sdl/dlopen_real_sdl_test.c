#include <dlfcn.h>
#include <stdio.h>

int main(void) {
    const char *path = "/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/libSDL2-2.0.0.8bitdo-real.dylib";
    void *sdl = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!sdl) {
        fprintf(stderr, "dlopen real SDL failed: %s\n", dlerror());
        return 1;
    }
    void *init = dlsym(sdl, "SDL_Init");
    printf("dlopen real SDL ok, SDL_Init=%p\n", init);
    dlclose(sdl);
    return 0;
}
