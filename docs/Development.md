# Development

## Build

```sh
make
```

or:

```sh
./scripts/build.sh
```

## Main Source

```text
src/sdlshim/8bitdo_sdlshim.c
```

## Logs

The shim writes to:

```text
/tmp/8bitdo-sdlshim.log
```

Use:

```sh
./scripts/show-log.sh
```

## Test Programs

SDL ladder:

```sh
./scripts/test-sdl-ladder.sh
```

GTA-style short-stop pattern:

```sh
./scripts/test-gta-pattern.sh
```

Direct Bluetooth output report ladder:

```sh
make build/bt_raw_all4_ladder
./build/bt_raw_all4_ladder 20 40 60 80 100 255
```

## Suggested Improvements

- Replace bundle patching with a cleaner Wine/CrossOver override.
- Add per-game profiles.
- Add a curve editor.
- Decode the firmware motor scheduler more precisely.
- Make firmware patching reproducible from official firmware files without shipping firmware blobs.
- Test other games and controller firmware versions.
- Explore an upstream SDL HIDAPI path.
- Explore a native macOS GameController haptics route.

## Privacy Checklist Before Publishing

Before pushing, run:

```sh
rg -n -i 'your-name|real-name|home-directory|local-workspace|downloads|private' .
find . -type f \( -name '*.log' -o -name '*.trace' -o -name '*.pid' -o -name '*.dylib' -o -name '*.dat' -o -name '*.bin' -o -name '*.app' \) -print
```

This repo is intended to contain source and notes only, not local traces or vendor binaries.
