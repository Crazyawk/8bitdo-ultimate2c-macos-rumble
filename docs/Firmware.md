# Firmware

This project currently depends on a firmware-side Bluetooth output-report path. Firmware binaries are not included in this repository.

In plain terms: the CrossOver shim alone is not enough for a stock controller. The controller must also run firmware that handles the Bluetooth output report and maps it to the motors.

## Hardware IDs

Normal Bluetooth HID:

```text
VID: 0x2dc8
PID: 0x301b
```

Bootloader USB mode:

```text
VID: 0x2dc8
PID: 0x3208
```

## Current Understanding

The controller firmware contains a BLE HID output report area. The working local route wired an output-report write callback to the controller's motor scheduler.

The stable local firmware build used during testing was named:

```text
ultimate2c-controller-v1.09-bt-rumble-proportional-latch30.dat
```

That file is not included here.

## Generating The Patched Firmware Locally

The repo includes a helper that tries to fetch the official v1.09 firmware and generate the patched firmware locally:

```sh
./scripts/prepare-patched-firmware.sh
```

Output:

```text
build/firmware/ultimate2c-controller-v1.09-bt-rumble-proportional-latch30.dat
```

Known SHA-256 for the patched firmware generated from the tested official v1.09 input:

```text
0b7676f8a6576cac4a63a8a6baea19cd3e712cf4d7582f10e34ee80fdcca2f58
```

If the automatic official firmware download does not work, provide the official `.dat` yourself:

```sh
OFFICIAL_FIRMWARE="/path/to/official-ultimate2c-controller-v1.09.dat" ./scripts/prepare-patched-firmware.sh
```

Patch chain:

```text
official v1.09
  -> patch_ultimate2c_bt_rumble_len.py
  -> patch_ultimate2c_bt_output_id1.py
  -> patch_ultimate2c_bt_rumble_proportional.py
  -> ultimate2c-controller-v1.09-bt-rumble-proportional-latch30.dat
```

## Flashing

Build the firmware tools:

```sh
./scripts/build-firmware-tools.sh
```

The flash script refuses to run unless the user explicitly accepts brick risk:

```sh
I_UNDERSTAND_BRICK_RISK=1 ./scripts/flash-patched-firmware.sh
```

The default firmware path is:

```text
build/firmware/ultimate2c-controller-v1.09-bt-rumble-proportional-latch30.dat
```

## Stable Handler Shape

The stable handler reads the BLE ATT write payload at callback pointer `+9`, then computes:

```text
low  = max(dat[0], dat[2])
high = max(dat[1], dat[3])
```

For nonzero rumble it calls the firmware motor scheduler roughly like:

```text
motor_schedule(low, high, 30)
```

For stop:

```text
motor_schedule(0, 0, 1)
```

## Known Bad Experiment

A value-buffer firmware experiment caused disconnect/reconnect behavior on output reports. Do not use that approach without deeper review.

## Included Firmware Tools

`tools/firmware/` contains patching and bootloader research tools. These are included for review and reproduction, not as a polished consumer flashing workflow.

Important files:

```text
patch_ultimate2c_bt_output_id1.py
patch_ultimate2c_bt_rumble_proportional.py
ultimate2c_boot_backup.c
ultimate2c_boot_flash.c
```

## Safety Notes

Do not flash anything unless:

- you understand the bootloader protocol
- you have a controller recovery plan
- you accept the risk of bricking the controller
- you know exactly which firmware file you are patching

Raw notes: [ULTIMATE2C_FIRMWARE_NOTES.md](ULTIMATE2C_FIRMWARE_NOTES.md)
