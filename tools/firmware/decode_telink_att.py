#!/usr/bin/env python3
import argparse
import struct
from pathlib import Path


UUID_NAMES = {
    0x1800: "GAP",
    0x1801: "GATT",
    0x180A: "DEVICE_INFO",
    0x180F: "BATTERY_SERVICE",
    0x1812: "HID_SERVICE",
    0x2800: "PRIMARY_SERVICE",
    0x2803: "CHARACTERISTIC",
    0x2902: "CCCD",
    0x2908: "REPORT_REF",
    0x2A00: "DEV_NAME",
    0x2A01: "APPEARANCE",
    0x2A04: "PERIPH_CONN",
    0x2A05: "SERVICE_CHANGED",
    0x2A19: "BATTERY",
    0x2A29: "MANUFACTURER",
    0x2A32: "BOOT_KBD_OUTPUT",
    0x2A4A: "HID_INFO",
    0x2A4B: "REPORT_MAP",
    0x2A4C: "HID_CONTROL",
    0x2A4D: "REPORT",
    0x2A4E: "PROTOCOL_MODE",
    0x2A50: "PNP_ID",
}


def u16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def decode(path, table_offset, image_base, wrapper_size):
    data = Path(path).read_bytes()
    shift = image_base - wrapper_size

    def ptr_to_offset(ptr):
        off = ptr - shift
        if 0 <= off < len(data):
            return off
        return None

    count = u16(data, table_offset)
    print(f"{Path(path).name}: att_count={count} table_offset=0x{table_offset:x}")
    print("idx handle off    att perm uuidLen len  uuid                  value")

    for idx in range(count + 1):
        off = table_offset + idx * 24
        att_num, perm, uuid_len, attr_len, uuid_ptr, value_ptr, write_cb, read_cb = struct.unpack_from(
            "<HBBIIIII", data, off
        )
        uuid_off = ptr_to_offset(uuid_ptr)
        value_off = ptr_to_offset(value_ptr)
        uuid_value = None
        uuid_name = ""

        if uuid_off is not None and uuid_len == 2:
            uuid_value = u16(data, uuid_off)
            uuid_name = UUID_NAMES.get(uuid_value, hex(uuid_value))

        value_desc = ""
        if value_off is not None and uuid_value == 0x2800 and attr_len >= 2:
            svc = u16(data, value_off)
            value_desc = f"svc={UUID_NAMES.get(svc, hex(svc))}"
        elif value_off is not None and uuid_value == 0x2803 and attr_len >= 5:
            props = data[value_off]
            handle = u16(data, value_off + 1)
            char_uuid = u16(data, value_off + 3)
            value_desc = (
                f"props=0x{props:02x} value_handle=0x{handle:04x} "
                f"uuid={UUID_NAMES.get(char_uuid, hex(char_uuid))}"
            )
        elif value_off is not None and uuid_value == 0x2908 and attr_len >= 2:
            value_desc = f"report_id={data[value_off]} report_type={data[value_off + 1]}"
        elif value_off is not None:
            preview = data[value_off : value_off + min(attr_len, 16)].hex(" ")
            value_desc = preview
        else:
            value_desc = f"ram_or_external=0x{value_ptr:06x}"

        uuid_desc = "none"
        if uuid_value is not None:
            uuid_desc = f"0x{uuid_value:04x} {uuid_name}"
        elif uuid_off is not None:
            uuid_desc = data[uuid_off : uuid_off + uuid_len].hex(" ")

        callbacks = ""
        if write_cb or read_cb:
            callbacks = f" w=0x{write_cb:x} r=0x{read_cb:x}"

        print(
            f"{idx:02d}  0x{idx:02x}   0x{off:05x} {att_num:03x} "
            f"0x{perm:02x} {uuid_len:7d} {attr_len:03x} {uuid_desc:22s} "
            f"{value_desc}{callbacks}"
        )


def main():
    parser = argparse.ArgumentParser(description="Decode Telink attribute_t tables in 8BitDo firmware blobs.")
    parser.add_argument("firmware")
    parser.add_argument("--table-offset", type=lambda x: int(x, 0), default=0x11458)
    parser.add_argument("--image-base", type=lambda x: int(x, 0), default=0x6000)
    parser.add_argument("--wrapper-size", type=lambda x: int(x, 0), default=0x1C)
    args = parser.parse_args()
    decode(args.firmware, args.table_offset, args.image_base, args.wrapper_size)


if __name__ == "__main__":
    main()
