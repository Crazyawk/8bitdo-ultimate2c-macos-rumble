#include <SDL2/SDL.h>
#include <stdio.h>

int main(void) {
    if (!SDL_getenv("SDL_JOYSTICK_HIDAPI")) {
        SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI, "1");
    }
    if (!SDL_getenv("SDL_JOYSTICK_MFI")) {
        SDL_SetHint(SDL_HINT_JOYSTICK_MFI, "0");
    }
    if (!SDL_getenv("SDL_JOYSTICK_IOKIT")) {
        SDL_SetHint(SDL_HINT_JOYSTICK_IOKIT, "1");
    }
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");

    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC | SDL_INIT_JOYSTICK) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    int count = SDL_NumJoysticks();
    printf("SDL version %d.%d.%d, joysticks=%d\n",
        SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL, count);

    int result = 2;
    for (int i = 0; i < count; i++) {
        const char *name = SDL_JoystickNameForIndex(i);
        SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(i);
        char guid_string[64];
        SDL_JoystickGetGUIDString(guid, guid_string, sizeof(guid_string));
        printf("[%d] %s guid=%s controller=%s\n", i, name ? name : "(null)",
            guid_string, SDL_IsGameController(i) ? "yes" : "no");

        if (!SDL_IsGameController(i)) {
            continue;
        }

        SDL_GameController *controller = SDL_GameControllerOpen(i);
        if (!controller) {
            printf("  open failed: %s\n", SDL_GetError());
            continue;
        }

        printf("  controller name=%s rumble=%s trigger_rumble=%s\n",
            SDL_GameControllerName(controller),
            SDL_GameControllerHasRumble(controller) ? "yes" : "no",
            SDL_GameControllerHasRumbleTriggers(controller) ? "yes" : "no");

        int rumble = SDL_GameControllerRumble(controller, 0xffff, 0xffff, 1000);
        printf("  SDL_GameControllerRumble => %d (%s)\n", rumble, SDL_GetError());
        SDL_Delay(1100);
        int trigger = SDL_GameControllerRumbleTriggers(controller, 0xffff, 0xffff, 700);
        printf("  SDL_GameControllerRumbleTriggers => %d (%s)\n", trigger, SDL_GetError());
        SDL_Delay(800);

        if (rumble == 0 || trigger == 0) {
            result = 0;
        }
        SDL_GameControllerClose(controller);
    }

    SDL_Quit();
    return result;
}
