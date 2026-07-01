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
TARGET_SERVICE_SUFFIX = "1912"
TARGET_CHAR_SUFFIX = "2B12"


def run_for(seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        NSRunLoop.currentRunLoop().runUntilDate_(
            NSDate.dateWithTimeIntervalSinceNow_(0.05)
        )


def nsdata(raw):
    return NSData.dataWithBytes_length_(raw, len(raw))


def uuid_str(obj):
    return str(obj.UUIDString()).upper()


class Delegate(NSObject):
    def init(self):
        self = objc.super(Delegate, self).init()
        self.central = None
        self.peripheral = None
        self.characteristic = None
        self.connected = False
        self.ready = False
        return self

    def centralManagerDidUpdateState_(self, central):
        print(f"central state={central.state()}", flush=True)
        if central.state() == CBManagerStatePoweredOn:
            services = [CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)]
            connected = central.retrieveConnectedPeripheralsWithServices_(services)
            print(f"connected candidates={len(connected)}", flush=True)
            for peripheral in connected:
                name = str(peripheral.name() or "")
                print(f"connected candidate {name!r} uuid={peripheral.identifier()}", flush=True)
                if TARGET_NAME in name or "8BitDo" in name:
                    self.peripheral = peripheral
                    peripheral.setDelegate_(self)
                    central.connectPeripheral_options_(peripheral, None)
                    return
            central.scanForPeripheralsWithServices_options_(None, None)
            print("scanning", flush=True)

    def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(
        self, central, peripheral, advertisementData, RSSI
    ):
        name = str(peripheral.name() or advertisementData.get("kCBAdvDataLocalName", "") or "")
        if TARGET_NAME in name or "8BitDo" in name:
            print(f"target {name!r} rssi={RSSI} uuid={peripheral.identifier()}", flush=True)
            self.peripheral = peripheral
            peripheral.setDelegate_(self)
            central.stopScan()
            central.connectPeripheral_options_(peripheral, None)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        print("connected", flush=True)
        self.connected = True
        peripheral.discoverServices_(None)

    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            print(f"service error {error}", flush=True)
            return
        for service in peripheral.services() or []:
            print(f"service {uuid_str(service.UUID())}", flush=True)
            peripheral.discoverCharacteristics_forService_(None, service)

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        if error:
            print(f"char error {error}", flush=True)
            return
        for ch in service.characteristics() or []:
            su = uuid_str(service.UUID())
            cu = uuid_str(ch.UUID())
            print(f"  char {cu} props=0x{ch.properties():x}", flush=True)
            if su.endswith(TARGET_SERVICE_SUFFIX) and cu.endswith(TARGET_CHAR_SUFFIX):
                self.characteristic = ch
                peripheral.setNotifyValue_forCharacteristic_(True, ch)
                peripheral.readValueForCharacteristic_(ch)
                self.ready = True

    def peripheral_didUpdateValueForCharacteristic_error_(self, peripheral, characteristic, error):
        if error:
            print(f"notify/read error {error}", flush=True)
            return
        value = characteristic.value()
        raw = bytes(value) if value is not None else b""
        print(f"value {uuid_str(characteristic.UUID())} len={len(raw)} {raw.hex(' ')}", flush=True)


def main():
    delegate = Delegate.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    delegate.central = central
    run_for(8)
    if not delegate.ready:
        print("target characteristic not ready", flush=True)
        return 2

    tests = [
        ("hid_report_5_4x64", bytes([0x05, 64, 64, 64, 64])),
        ("hid_report_5_4x100", bytes([0x05, 100, 100, 100, 100])),
        ("raw_4x100", bytes([100, 100, 100, 100])),
        ("raw_4xff", bytes([0xFF, 0xFF, 0xFF, 0xFF])),
        ("zero_5", bytes([0x05, 0, 0, 0, 0])),
        ("zero_4", bytes([0, 0, 0, 0])),
    ]
    for label, payload in tests:
        print(f"WRITE {label}: {payload.hex(' ')}", flush=True)
        delegate.peripheral.writeValue_forCharacteristic_type_(
            nsdata(payload), delegate.characteristic, CBCharacteristicWriteWithoutResponse
        )
        run_for(1.0)

    if delegate.peripheral:
        central.cancelPeripheralConnection_(delegate.peripheral)
    return 0


if __name__ == "__main__":
    sys.exit(main())
