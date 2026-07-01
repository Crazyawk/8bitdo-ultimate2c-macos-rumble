# Reverse Engineering Notes

This file summarizes the path that led to the current proof-of-concept.

## Initial Symptoms

- The controller worked over Bluetooth as a game controller.
- CrossOver exposed it as XInput.
- Rumble did not work through normal CrossOver/GTA behavior.
- Dongle and wired modes behaved differently from Bluetooth and were not viable for the target setup.

## macOS Findings

macOS reported the Bluetooth controller as a BLE HID gamepad with:

```text
VendorID:  0x2dc8
ProductID: 0x301b
```

The Bluetooth HID object had `MaxOutputReportSize = 5`, which matched the idea of a report ID plus four output bytes.

## Direct HID Tests

The direct HID ladder tool sends raw output reports through IOKit:

```sh
./build/bt_raw_all4_ladder 20 40 60 80 100 255
```

Observed physical response:

```text
20 -> 40 -> 60: noticeable climb
60 -> 80 -> 100: mostly plateau
255: stronger impact bucket
```

## CrossOver / GTA Findings

The SDL shim log showed GTA sending real rumble requests. Example:

```text
SDL_JoystickRumble raw=[0 105] out=[36 54] dur=1000
SDL_JoystickRumbleTriggers raw=[0 0] out=[0 0] dur=1000
SDL_JoystickRumble raw=[0 0] out=[0 0] dur=0
```

Two quirks mattered:

- zero trigger rumble should not cancel main rumble
- GTA sometimes sends very fast stop calls, so the shim adds a short minimum hold

## Useful Tools

```text
tools/bt-hid/
tools/sdl/
tools/dongle/
tools/gamecontroller/
tools/hidtrace/
tools/firmware/
```

Many tools are rough research probes. They are intentionally preserved so other people can reproduce or improve the investigation.

