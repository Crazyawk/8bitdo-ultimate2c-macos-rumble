#!/usr/bin/env python3
import sys
import time

import objc
from Foundation import NSObject, NSData, NSRunLoop, NSDate
from CoreBluetooth import (
    CBCentralManager,
    CBUUID,
    CBCharacteristicWriteWithoutResponse,
    CBManagerStatePoweredOn,
)

TARGET_NAME = "8BitDo Ultimate 2C Wireless"
TARGET_SERVICE_UUID = "00010203-0405-0607-0809-0A0B0C0D1912"
TARGET_CHAR_SUFFIX = "2B12"


def run_for(seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        NSRunLoop.currentRunLoop().runUntilDate_(
            NSDate.dateWithTimeIntervalSinceNow_(0.05)
        )


def uuid_str(obj):
    return str(obj.UUIDString()).upper()


class Delegate(NSObject):
    def init(self):
        self = objc.super(Delegate, self).init()
        self.central = None
        self.peripheral = None
        self.characteristic = None
        self.ready = False
        return self

    def centralManagerDidUpdateState_(self, central):
        print(f"central state={central.state()}", flush=True)
        if central.state() == CBManagerStatePoweredOn:
            services = [CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)]
            for peripheral in central.retrieveConnectedPeripheralsWithServices_(services):
                name = str(peripheral.name() or "")
                print(f"candidate {name!r} uuid={peripheral.identifier()}", flush=True)
                if TARGET_NAME in name or "8BitDo" in name:
                    self.peripheral = peripheral
                    peripheral.setDelegate_(self)
                    central.connectPeripheral_options_(peripheral, None)
                    return
            central.scanForPeripheralsWithServices_options_(services, None)

    def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(
        self, central, peripheral, advertisementData, RSSI
    ):
        name = str(peripheral.name() or advertisementData.get("kCBAdvDataLocalName", "") or "")
        print(f"found {name!r} rssi={RSSI}", flush=True)
        if TARGET_NAME in name or "8BitDo" in name:
            self.peripheral = peripheral
            peripheral.setDelegate_(self)
            central.stopScan()
            central.connectPeripheral_options_(peripheral, None)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        print("connected", flush=True)
        peripheral.discoverServices_([CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)])

    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            print(f"service error {error}", flush=True)
            return
        for service in peripheral.services() or []:
            peripheral.discoverCharacteristics_forService_(None, service)

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        if error:
            print(f"char error {error}", flush=True)
            return
        for ch in service.characteristics() or []:
            print(f"char {uuid_str(ch.UUID())} props=0x{ch.properties():x}", flush=True)
            if uuid_str(ch.UUID()).endswith(TARGET_CHAR_SUFFIX):
                self.characteristic = ch
                peripheral.setNotifyValue_forCharacteristic_(True, ch)
                self.ready = True

    def peripheral_didUpdateValueForCharacteristic_error_(self, peripheral, characteristic, error):
        if error:
            print(f"notify/read error {error}", flush=True)
            return
        value = characteristic.value()
        raw = bytes(value) if value is not None else b""
        print(f"notify len={len(raw)} {raw.hex(' ')}", flush=True)


def parse_payload(arg):
    clean = arg.replace(":", " ").replace(",", " ")
    return bytes(int(part, 16) for part in clean.split())


def main():
    if len(sys.argv) < 2:
        print("usage: ble_one_write.py '05 64 64 64 64' [repeat]", file=sys.stderr)
        return 2
    payload = parse_payload(sys.argv[1])
    repeat = int(sys.argv[2]) if len(sys.argv) > 2 else 1

    delegate = Delegate.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    delegate.central = central
    run_for(6)
    if not delegate.ready:
        print("not ready", flush=True)
        return 3

    data = NSData.dataWithBytes_length_(payload, len(payload))
    for i in range(repeat):
        print(f"write {i + 1}/{repeat}: {payload.hex(' ')}", flush=True)
        delegate.peripheral.writeValue_forCharacteristic_type_(
            data, delegate.characteristic, CBCharacteristicWriteWithoutResponse
        )
        run_for(0.75)
    run_for(0.5)
    if delegate.peripheral:
        central.cancelPeripheralConnection_(delegate.peripheral)
    return 0


if __name__ == "__main__":
    sys.exit(main())
