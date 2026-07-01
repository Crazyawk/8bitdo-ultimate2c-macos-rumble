#!/usr/bin/env python3
import argparse
import struct
import zlib
from pathlib import Path


WRAPPER_SIZE = 0x1C
IMAGE_BASE = 0x6000

BT_OUTPUT_ATTR_OFFSET = 0x11770
BT_OUTPUT_WRITE_CB_OFFSET = BT_OUTPUT_ATTR_OFFSET + 16

CODE_ADDR = 0x18274
CODE_OFFSET = WRAPPER_SIZE + (CODE_ADDR - IMAGE_BASE)

# TC32 handler linked for CODE_ADDR.
#
# int bt_output_write(void *att_packet) {
#     uint8_t *b = (uint8_t *)0x84315c;  // output REPORT value buffer
#     firmware_motor_schedule(b[0], b[1], 30);
#     return 0;
# }
#
# The previous proportional handler read from att_packet + 9. The controller
# accepted writes, but every strength felt identical, which strongly suggests
# that offset was reading ATT/GATT envelope bytes instead of the report value.
HANDLER = bytes.fromhex(
    "10 65"              # push {r4, lr}
    "c0 46"              # align the following PC-relative literal load
    "06 0b"              # r3 = literal @ +0x20 (0x84315c)
    "48 18"              # r0 = *(uint8_t *)r3
    "b3 01"              # r3 += 1
    "48 19"              # r1 = *(uint8_t *)r3
    "1e a2"              # r2 = 30
    "c0 46"              # align the following PC-relative literal load
    "04 0b"              # r3 = literal @ +0x24 (0xfa68)
    "00 90 05 98"        # call r3
    "00 a0"              # r0 = 0
    "10 6d"              # pop {r4, pc}
    "c0 46 c0 46 c0 46"
    "5c 31 84 00"        # output report value buffer
    "68 fa 00 00"        # firmware_motor_schedule
)


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

    if data[CODE_OFFSET : CODE_OFFSET + len(HANDLER)] != b"\x00" * len(HANDLER):
        raise SystemExit(f"code cave at 0x{CODE_OFFSET:x} is not empty")

    attr = struct.unpack_from("<HBBIIIII", data, BT_OUTPUT_ATTR_OFFSET)
    _att_num, perm, uuid_len, attr_len, _uuid_ptr, value_ptr, write_cb, _read_cb = attr
    if perm != 0x03 or uuid_len != 2 or attr_len != 4 or value_ptr != 0x84315C:
        raise SystemExit(f"unexpected output-report attribute tuple: {attr!r}")
    if write_cb != 0:
        raise SystemExit(f"output-report write callback already set: 0x{write_cb:x}")

    data[CODE_OFFSET : CODE_OFFSET + len(HANDLER)] = HANDLER
    struct.pack_into("<I", data, BT_OUTPUT_WRITE_CB_OFFSET, CODE_ADDR | 1)

    stored_crc = update_telink_crc(data)
    dst.write_bytes(data)

    print(f"patched {src} -> {dst}")
    print(f"handler @ file 0x{CODE_OFFSET:x}, firmware 0x{CODE_ADDR:x}, {len(HANDLER)} bytes")
    print(f"write callback @0x{BT_OUTPUT_WRITE_CB_OFFSET:x}: 0x00000000 -> 0x{CODE_ADDR | 1:08x}")
    print(f"updated Telink CRC: 0x{stored_crc:08x}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Wire the Ultimate 2C BLE output report value buffer to proportional motor strengths."
    )
    parser.add_argument(
        "src",
        nargs="?",
        default="work/firmware-research/ultimate2c-controller-v1.09-bt-output-id1.dat",
    )
    parser.add_argument(
        "dst",
        nargs="?",
        default="work/firmware-research/ultimate2c-controller-v1.09-bt-rumble-valuebuf-latch30.dat",
    )
    args = parser.parse_args()
    patch_firmware(Path(args.src), Path(args.dst))


if __name__ == "__main__":
    main()
