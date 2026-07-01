#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path


WRAPPER_SIZE = 0x1C
IMAGE_BASE = 0x6000
OUTPUT_REPORT_REF_PTR_OFFSET = 0x11794
STATIC_OUTPUT_REPORT_REF_OFFSET = 0x112A8  # bytes: 05 02


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
    if data[STATIC_OUTPUT_REPORT_REF_OFFSET:STATIC_OUTPUT_REPORT_REF_OFFSET + 2] != b"\x05\x02":
        raise SystemExit("static output report reference bytes not found")

    old_ptr = struct.unpack_from("<I", data, OUTPUT_REPORT_REF_PTR_OFFSET)[0]
    new_ptr = IMAGE_BASE + STATIC_OUTPUT_REPORT_REF_OFFSET - WRAPPER_SIZE
    struct.pack_into("<I", data, OUTPUT_REPORT_REF_PTR_OFFSET, new_ptr)
    stored_crc = update_telink_crc(data)

    dst.write_bytes(data)
    print(f"patched {src} -> {dst}")
    print(f"report ref pAttrValue @0x{OUTPUT_REPORT_REF_PTR_OFFSET:x}: 0x{old_ptr:08x} -> 0x{new_ptr:08x}")
    print(f"updated Telink CRC: 0x{stored_crc:08x}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Patch Ultimate 2C BLE Report 5 reference to static output-report tag [05 02]."
    )
    parser.add_argument(
        "src",
        nargs="?",
        default="build/firmware/ultimate2c-controller-v1.09-bt-output-len4.dat",
    )
    parser.add_argument(
        "dst",
        nargs="?",
        default="build/firmware/ultimate2c-controller-v1.09-bt-output-len4-reportref.dat",
    )
    args = parser.parse_args()
    patch_firmware(Path(args.src), Path(args.dst))


if __name__ == "__main__":
    main()
