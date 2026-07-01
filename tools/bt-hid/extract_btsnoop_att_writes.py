#!/usr/bin/env python3
import argparse
import struct
import sys
from collections import defaultdict


BTSNOOP_EPOCH_DELTA_US = 0x00E03AB44A676000
ATT_OPS = {
    0x12: "write_req",
    0x52: "write_cmd",
    0x16: "prepare_write_req",
    0x18: "execute_write_req",
    0x1B: "notification",
    0x1D: "indication",
}


def read_records(path):
    with open(path, "rb") as f:
        header = f.read(16)
        if len(header) != 16 or not header.startswith(b"btsnoop\0"):
            raise SystemExit(f"{path}: not a btsnoop file")
        while True:
            rec_header = f.read(24)
            if not rec_header:
                return
            if len(rec_header) != 24:
                raise SystemExit("truncated btsnoop record header")
            orig_len, inc_len, flags, drops, timestamp = struct.unpack(">IIIIq", rec_header)
            data = f.read(inc_len)
            if len(data) != inc_len:
                raise SystemExit("truncated btsnoop record body")
            yield {
                "orig_len": orig_len,
                "inc_len": inc_len,
                "flags": flags,
                "drops": drops,
                "timestamp_us": timestamp - BTSNOOP_EPOCH_DELTA_US,
                "data": data,
            }


def direction(flags):
    # Android btsnoop usually uses bit 0: 0 = sent, 1 = received.
    return "rx" if (flags & 1) else "tx"


def parse_acl(packet):
    if not packet:
        return None

    # Most Android btsnoop files include the H4 packet type byte.
    if packet[0] == 0x02:
        packet = packet[1:]
    elif packet[0] in (0x01, 0x03, 0x04):
        return None

    if len(packet) < 8:
        return None
    handle_flags, hci_len = struct.unpack_from("<HH", packet, 0)
    if len(packet) < 4 + hci_len:
        return None
    handle = handle_flags & 0x0FFF
    pb_flag = (handle_flags >> 12) & 0x3
    bc_flag = (handle_flags >> 14) & 0x3
    payload = packet[4 : 4 + hci_len]
    return handle, pb_flag, bc_flag, payload


def parse_l2cap_first(payload):
    if len(payload) < 4:
        return None
    l2_len, cid = struct.unpack_from("<HH", payload, 0)
    body = payload[4 : 4 + l2_len]
    return l2_len, cid, body


def handle_att(record, args):
    acl = parse_acl(record["data"])
    if not acl:
        return None
    conn_handle, pb_flag, _bc_flag, payload = acl

    # 0b10 is first non-automatically-flushable packet; 0b00 is continuation.
    if pb_flag not in (0x2, 0x0):
        return None
    first = parse_l2cap_first(payload)
    if not first:
        return None
    _l2_len, cid, att = first
    if cid != 0x0004 or not att:
        return None

    op = att[0]
    if op not in ATT_OPS:
        return None
    name = ATT_OPS[op]
    out = {
        "time_us": record["timestamp_us"],
        "dir": direction(record["flags"]),
        "conn": conn_handle,
        "op": op,
        "name": name,
        "handle": None,
        "value": b"",
        "raw": att,
    }
    if op in (0x12, 0x52, 0x16, 0x1B, 0x1D) and len(att) >= 3:
        out["handle"] = struct.unpack_from("<H", att, 1)[0]
        out["value"] = att[3:]
    elif op == 0x18:
        out["value"] = att[1:]
    return out


def main():
    parser = argparse.ArgumentParser(
        description="Extract BLE ATT writes/notifications from Android btsnoop_hci logs."
    )
    parser.add_argument("btsnoop")
    parser.add_argument("--all", action="store_true", help="also print notifications/indications")
    parser.add_argument("--min-len", type=int, default=1)
    args = parser.parse_args()

    counts = defaultdict(int)
    printed = 0
    for record in read_records(args.btsnoop):
        event = handle_att(record, args)
        if not event:
            continue
        counts[event["name"]] += 1
        if not args.all and event["name"] not in ("write_req", "write_cmd", "prepare_write_req", "execute_write_req"):
            continue
        if len(event["value"]) < args.min_len:
            continue
        handle = "" if event["handle"] is None else f" handle=0x{event['handle']:04x}"
        value = event["value"].hex(" ")
        raw = event["raw"].hex(" ")
        print(
            f"{event['time_us']/1_000_000:12.6f} {event['dir']} "
            f"conn=0x{event['conn']:04x} {event['name']}{handle} "
            f"len={len(event['value'])} value={value} raw={raw}"
        )
        printed += 1

    print("\nsummary:", file=sys.stderr)
    for name in sorted(counts):
        print(f"  {name}: {counts[name]}", file=sys.stderr)
    print(f"  printed: {printed}", file=sys.stderr)


if __name__ == "__main__":
    main()
