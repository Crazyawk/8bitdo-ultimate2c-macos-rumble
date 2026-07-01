#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path


WRAPPER_SIZE = 0x1C
OUTPUT_REPORT_ATTR_LEN_OFFSET = 0x11774


def patch_firmware(src: Path, dst: Path) -> None:
    data = bytearray(src.read_bytes())
    if data[OUTPUT_REPORT_ATTR_LEN_OFFSET] != 0x01:
        raise SystemExit(
            f"expected attr_len byte 0x01 at 0x{OUTPUT_REPORT_ATTR_LEN_OFFSET:x}, "
            f"found 0x{data[OUTPUT_REPORT_ATTR_LEN_OFFSET]:02x}"
        )

    data[OUTPUT_REPORT_ATTR_LEN_OFFSET] = 0x04

    payload = memoryview(data)[WRAPPER_SIZE:]
    if payload[0x08:0x0C].tobytes() != b"KNLT":
        raise SystemExit("not a Telink KNLT firmware payload")

    hsize = struct.unpack_from("<I", payload, 0x18)[0]
    if hsize > len(payload) or (hsize & 0x0F) != 4:
        raise SystemExit(f"invalid Telink hsize 0x{hsize:x}")

    crc_without_word = zlib.crc32(payload[: hsize - 4]) & 0xFFFFFFFF
    stored_crc = (~crc_without_word) & 0xFFFFFFFF
    struct.pack_into("<I", data, WRAPPER_SIZE + hsize - 4, stored_crc)

    dst.write_bytes(data)
    print(f"patched {src} -> {dst}")
    print(f"changed attr_len @0x{OUTPUT_REPORT_ATTR_LEN_OFFSET:x}: 0x01 -> 0x04")
    print(f"updated Telink CRC @0x{WRAPPER_SIZE + hsize - 4:x}: 0x{stored_crc:08x}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Patch 8BitDo Ultimate 2C Wireless BLE HID output report length from 1 to 4."
    )
    parser.add_argument(
        "src",
        nargs="?",
        default="build/firmware/official/ultimate2c-controller-v1.09.dat",
    )
    parser.add_argument(
        "dst",
        nargs="?",
        default="build/firmware/ultimate2c-controller-v1.09-bt-output-len4.dat",
    )
    args = parser.parse_args()
    patch_firmware(Path(args.src), Path(args.dst))


if __name__ == "__main__":
    main()
