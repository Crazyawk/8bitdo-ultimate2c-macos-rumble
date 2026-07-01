# 8BitDo Ultimate 2C Wireless Firmware Notes

These are historical Bluetooth-rumble firmware investigation notes. Some sections were written before the stable firmware-side rumble experiment succeeded, so treat this file as raw research context rather than a polished flashing guide. See `Firmware.md` for the current summary.

## Official firmware files pulled

The old 8BitDo firmware API lists the Ultimate 2C Wireless controller as type `75` and the adapter as type `76`.

Downloaded files:

- `official/ultimate2c-controller-v1.09.dat`
- `official/ultimate2c-controller-v1.06.dat`
- `official/ultimate2c-adapter-v1.03.dat`
- `official/ultimate2c-adapter-v1.00.dat`

Local hashes:

```text
MD5 (ultimate2c-adapter-v1.00.dat) = 934525490551334ab0cc904f1cf57736
MD5 (ultimate2c-adapter-v1.03.dat) = 594b337c3fed92bfa3c5e8e462a36fcd
MD5 (ultimate2c-controller-v1.06.dat) = d8944c5f65a1cd1c310536d841607b79
MD5 (ultimate2c-controller-v1.09.dat) = b24d58a5b394c8480a44a6858959e80a
```

The old API's `md5` fields did not match these local hashes, so treat the API MD5 metadata as stale or unreliable.

## Controller v1.09 wrapper/header

`ultimate2c-controller-v1.09.dat` is `74780` bytes.

Important little-endian words:

```text
+0x00 version-ish:      0x0000006d (109)
+0x04 image_base:       0x00006000
+0x08 image_size:       0x00012400
+0x0c product_id:       0x0000301b
+0x10 check/signature?: 0xa420dcc3
+0x24 Telink mark:      0x544c4e4b ("KNLT" bytes / "TLNK" LE)
```

The `.dat` file has a `0x1c` byte wrapper before the firmware image, so firmware pointers map to file offsets by:

```text
file_offset = firmware_pointer - (0x6000 - 0x1c)
```

## Bluetooth HID/GATT table

The controller firmware contains a Telink SDK `attribute_t` GATT table at file offset `0x11458`.

Telink's documented layout is:

```c
typedef struct attribute {
    u16 attNum;
    u8  perm;
    u8  uuidLen;
    u32 attrLen;
    u8 *uuid;
    u8 *pAttrValue;
    att_readwrite_callback_t w;
    att_readwrite_callback_t r;
} attribute_t;
```

Useful decoded handles:

```text
0x0f PRIMARY_SERVICE HID_SERVICE
0x12 PROTOCOL_MODE, characteristic props 0x06
0x17 BOOT_KBD_OUTPUT, characteristic props 0x0e
0x19 REPORT, characteristic props 0x12
0x1d REPORT, characteristic props 0x12
0x21 REPORT, characteristic props 0x0e
0x24 REPORT_MAP, length 0x74, points to file offset 0x11368
0x29 HID_CONTROL, characteristic props 0x04
```

The report map at `0x11368` contains the known output report:

```text
05 0f 09 70 85 05 15 00 25 64 75 08 95 04 91 02
```

That is report ID `5`, physical-interface usage `0x70`, four 8-bit output values. This matches the 5-byte packet shape SDL's upstream 8BitDo HIDAPI code expects: report ID byte plus four motor bytes.

## Current interpretation

The firmware already declares a Bluetooth output report. macOS sees `MaxOutputReportSize = 5`, but `IOHIDDeviceSetReport` returns unsupported over the paired BLE HID path. The output report GATT characteristic also appears writable in firmware (`props=0x0e`, `perm=0x03`), so the bug is probably not just a missing HID report descriptor.

Likely firmware-level routes:

1. Add or expose a separate custom writable BLE characteristic that macOS/CoreBluetooth can access while the controller remains paired as HID, then handle that write in firmware and drive the motors.
2. Change Bluetooth identity/protocol to mimic a controller profile macOS already grants haptics/rumble support for.
3. Change the HID/GATT behavior enough that AppleUserHIDEventDriver accepts output reports over the normal HID path.

Route 1 is conceptually smallest, but still requires adding code and finding the motor-control path. Route 2 is much larger. Route 3 may be a macOS-side policy wall rather than a firmware descriptor bug.

## Safety state

Official recovery firmware can be downloaded, but that is not the same as a backup of a specific controller's current flash contents. A real backup requires proving that the bootloader exposes a read command and reading flash before writing anything.

Do not flash a modified `.dat` until:

- the firmware check/signature at `+0x10` is understood or bypassed by the official bootloader;
- the write protocol is captured or reproduced;
- readback/backup is attempted in bootloader mode, or the user explicitly accepts proceeding without a real backup.
