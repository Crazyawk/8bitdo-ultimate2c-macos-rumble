# Install

This document explains how to build, install, test, and restore the CrossOver SDL shim.

## Important: Shim Only vs Full Setup

The CrossOver shim can be built and installed from this repo.

Bluetooth rumble also requires the controller firmware to accept the output report used by the shim. Firmware binaries are not included. A stock controller with only the CrossOver shim installed will usually still have no Bluetooth rumble.

End-to-end setup is therefore:

1. Prepare or otherwise obtain a compatible patched controller firmware.
2. Flash the controller at your own risk.
3. Install the CrossOver SDL shim.
4. Test direct Bluetooth output reports.
5. Test CrossOver/game rumble.

## Requirements

- macOS
- Xcode command line tools
- Python 3
- Python `requests` package if using automatic firmware download
- CrossOver installed at `/Applications/CrossOver.app`, or `CROSSOVER_APP` set to a different path
- 8BitDo Ultimate 2C Wireless paired over Bluetooth
- A firmware-side Bluetooth output-report rumble path for full Bluetooth rumble

The current build target is `x86_64` because the tested CrossOver/Wine SDL path runs under Rosetta.

## Build

```sh
./scripts/build.sh
```

Or:

```sh
make
```

Build output goes to `build/`.

## Prepare Patched Firmware

This repo does not include firmware `.dat` files. To generate the tested patched firmware locally from an official firmware file:

```sh
./scripts/prepare-patched-firmware.sh
```

The script first tries to download the official Ultimate 2C Wireless controller v1.09 firmware using `tools/firmware/8bitdo-firmware.py`, then applies the patch chain into:

```text
build/firmware/ultimate2c-controller-v1.09-bt-rumble-proportional-latch30.dat
```

If automatic download fails, provide the official firmware manually:

```sh
OFFICIAL_FIRMWARE="/path/to/official-ultimate2c-controller-v1.09.dat" ./scripts/prepare-patched-firmware.sh
```

This prepares a patched `.dat`; it does not flash anything.

## Flash Patched Firmware

Firmware flashing can brick the controller. This is intentionally not automatic.

Build the firmware tools:

```sh
./scripts/build-firmware-tools.sh
```

Put the controller in BOOT mode, then run the flash script only if you accept the risk:

```sh
I_UNDERSTAND_BRICK_RISK=1 ./scripts/flash-patched-firmware.sh
```

You can also pass an explicit firmware path:

```sh
I_UNDERSTAND_BRICK_RISK=1 ./scripts/flash-patched-firmware.sh "/path/to/patched.dat"
```

See [Firmware.md](Firmware.md) before flashing.

## Install Into CrossOver

Fully quit GTA, any other CrossOver game, and CrossOver itself.

Then run:

```sh
./scripts/install-crossover-shim.sh
```

The installer:

1. Builds the shim.
2. Saves CrossOver's current SDL library as `libSDL2-2.0.0.8bitdo-real.dylib`.
3. Copies the shim over CrossOver's `libSDL2-2.0.0.dylib`.
4. Ad-hoc signs the changed dylibs.
5. Re-signs and verifies the CrossOver bundle.

If CrossOver is somewhere else:

```sh
CROSSOVER_APP="/path/to/CrossOver.app" ./scripts/install-crossover-shim.sh
```

## Restore CrossOver

Fully quit GTA, any other CrossOver game, and CrossOver itself.

Then run:

```sh
./scripts/restore-crossover-sdl.sh
```

This restores the saved real SDL library and re-signs CrossOver.

## Test

Check whether macOS sees the controller:

```sh
./scripts/check-controller.sh
```

Run an SDL ladder through the installed shim:

```sh
./scripts/test-sdl-ladder.sh
```

Run the GTA-style short-stop test:

```sh
./scripts/test-gta-pattern.sh
```

Show the shim log:

```sh
./scripts/show-log.sh
```

The shim log is:

```text
/tmp/8bitdo-sdlshim.log
```

## Ask For Help

Useful information to include:

- macOS version
- CrossOver version
- controller firmware version
- connection mode: Bluetooth, dongle, or wired
- whether direct Bluetooth output tests can rumble
- whether the SDL ladder can rumble
- recent `/tmp/8bitdo-sdlshim.log` lines

Useful commands:

```sh
./scripts/check-controller.sh
./scripts/show-log.sh 300
otool -L /Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/libSDL2-2.0.0.dylib
codesign -dv --verbose=2 /Applications/CrossOver.app/Contents/SharedSupport/CrossOver/lib64/libSDL2-2.0.0.dylib 2>&1
```
