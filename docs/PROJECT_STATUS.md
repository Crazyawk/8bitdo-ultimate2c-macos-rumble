# Project Status

This is the current working state from the local experiment.

## Hardware

- Controller: 8BitDo Ultimate 2C Wireless
- Bluetooth VID/PID seen by macOS: `0x2dc8:0x301b`
- Bootloader USB VID/PID seen by macOS: `0x2dc8:0x3208`
- macOS sees the normal Bluetooth controller as a gamepad, and CrossOver exposes it as XInput.

## Current solution shape

The working approach is two-part:

1. A patched controller firmware route that makes a BLE HID output report drive the motors.
2. A CrossOver SDL shim that intercepts game rumble calls and writes that output report through macOS IOKit HID.

The repo contains the shim source and research tools. It does not contain firmware `.dat` blobs or CrossOver SDL binaries.

A first-time user can build and install the shim, but Bluetooth rumble will not work on a stock controller unless they also generate and flash compatible firmware. The repo includes scripts for generating that patched firmware locally from an official firmware file, but the flash step remains experimental and risky.

## Current packet shape

The shim writes this output report:

```text
report type: Output
report id:   1
payload:     4 bytes [low, high, left, right]
```

The shim currently finds the Bluetooth controller by:

```text
VendorID:            0x2dc8
ProductID:           0x301b
Transport:           Bluetooth Low Energy
MaxOutputReportSize: 5
```

## Current feel calibration

Real-world feedback during testing:

- `20 -> 40 -> 60` felt like a noticeable climb.
- `60, 80, 100` mostly felt like a plateau.
- `255` felt stronger and works as a separate impact/overdrive bucket.

The current shim therefore compresses most normal GTA values into roughly `22..60`, and reserves `255` for near-max events.

## GTA/CrossOver behavior found in logs

GTA/CrossOver sends SDL rumble calls such as:

```text
SDL_JoystickRumble raw=[0 105] out=[36 54] dur=1000
SDL_JoystickRumbleTriggers raw=[0 0] out=[0 0] dur=1000
SDL_JoystickRumble raw=[0 0] out=[0 0] dur=0
```

Two important quirks:

- CrossOver often follows normal rumble with zero trigger rumble. The shim must not treat that as "stop all rumble."
- GTA often sends a normal stop very quickly after a pulse. The shim adds a short minimum hold so the physical motor has time to be felt.

## Known limitations

- This is not a clean upstream-quality driver yet.
- The firmware route is device-specific and risky.
- The exact firmware motor scheduler scale is not fully understood.
- The current CrossOver integration replaces SDL in the CrossOver app bundle, so CrossOver updates may undo it.
- The shim is x86_64 because CrossOver's Wine/SDL path is x86_64 under Rosetta.
- This has only been tested on one Mac and one controller.

## Things people could improve

- Find a macOS GameController framework route that exposes haptics without replacing SDL.
- Upstream a proper SDL HIDAPI quirk if there is a clean controller-side solution.
- Decode the firmware motor scheduler and make the strength curve truly linear.
- Make the firmware patch safer and reproducible from official firmware only.
- Avoid bundle patching by using a Wine/CrossOver override, loader path, or a small launcher that injects the shim.
- Add a small GUI for curve tuning and log inspection.
