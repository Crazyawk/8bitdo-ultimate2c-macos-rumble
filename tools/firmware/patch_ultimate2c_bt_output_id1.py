#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path


WRAPPER_SIZE = 0x1C
IMAGE_BASE = 0x6000

# In the HID report map: 05 0f 09 70 85 05 15 00 ...
# Change only the report ID byte for the output report.
REPORT_MAP_OUTPUT_ID_OFFSET = 0x113D0

# Attribute 34 is the Report Reference descriptor for the writable report
# characteristic. Point it at any stable in-image bytes equal to [01 02].
OUTPUT_REPORT_REF_PTR_OFFSET = 0x11794
STATIC_ID1_OUTPUT_REF_OFFSET = 0x119BA


def update_telink_crc(data: bytearray) -> int:
    payload = memoryview(data)[WRAPPER_SIZE:]
    if payload[0x08:0x0C].tobytes() != b"KNLT":
        raise SystemExit("not a Telink KNLT firmware payload")
    hsize = struct.unpack_from("<I", payload, 0x18)[0]
    if hsize > len(payload) or (hsize & 0x0F) != 4:
        raise SystemExit(f"invalid Telink hsize 0x{hsize:x}")
    crc_without_word = zlib.crc32(payload[: hsize - 4]) & 0xFFFFFFFF
    stored_crc = (~crc_without_word) & 0xFFFFFFFF
    struct.pack_into("<I", data, WRAPPER_SIZE + hsize - 4, stored_crc)
    return stored_crc


def patch_firmware(src: Path, dst: Path) -> None:
    data = bytearray(src.read_bytes())

    if data[REPORT_MAP_OUTPUT_ID_OFFSET - 5:REPORT_MAP_OUTPUT_ID_OFFSET + 3] != bytes.fromhex("05 0f 09 70 85 05 15 00"):
        raise SystemExit("unexpected HID report-map bytes around output report ID")
    if data[STATIC_ID1_OUTPUT_REF_OFFSET:STATIC_ID1_OUTPUT_REF_OFFSET + 2] != b"\x01\x02":
        raise SystemExit("static [01 02] report reference bytes not found")

    old_id = data[REPORT_MAP_OUTPUT_ID_OFFSET]
    data[REPORT_MAP_OUTPUT_ID_OFFSET] = 0x01

    old_ptr = struct.unpack_from("<I", data, OUTPUT_REPORT_REF_PTR_OFFSET)[0]
    new_ptr = IMAGE_BASE + STATIC_ID1_OUTPUT_REF_OFFSET - WRAPPER_SIZE
    struct.pack_into("<I", data, OUTPUT_REPORT_REF_PTR_OFFSET, new_ptr)

    stored_crc = update_telink_crc(data)
    dst.write_bytes(data)

    print(f"patched {src} -> {dst}")
    print(f"output report ID @0x{REPORT_MAP_OUTPUT_ID_OFFSET:x}: 0x{old_id:02x} -> 0x01")
    print(f"output Report Reference ptr @0x{OUTPUT_REPORT_REF_PTR_OFFSET:x}: 0x{old_ptr:08x} -> 0x{new_ptr:08x}")
    print(f"updated Telink CRC: 0x{stored_crc:08x}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Relabel Ultimate 2C BLE rumble output report from ID 5 to ID 1."
    )
    parser.add_argument(
        "src",
        nargs="?",
        default="build/firmware/ultimate2c-controller-v1.09-bt-output-len4.dat",
    )
    parser.add_argument(
        "dst",
        nargs="?",
        default="build/firmware/ultimate2c-controller-v1.09-bt-output-id1.dat",
    )
    args = parser.parse_args()
    patch_firmware(Path(args.src), Path(args.dst))


if __name__ == "__main__":
    main()
