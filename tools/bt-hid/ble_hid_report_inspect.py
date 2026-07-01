#!/usr/bin/env python3
import argparse
import sys
import time

import objc
from Foundation import NSDate, NSObject, NSRunLoop
from CoreBluetooth import (
    CBCentralManager,
    CBUUID,
    CBManagerStatePoweredOn,
    CBCharacteristicPropertyRead,
    CBCharacteristicPropertyWrite,
    CBCharacteristicPropertyWriteWithoutResponse,
)


HID_SERVICE = "1812"
REPORT_CHAR = "2A4D"
REPORT_MAP_CHAR = "2A4B"
REPORT_REFERENCE_DESC = "2908"
TARGET_HINTS = ("8BitDo", "Ultimate 2C")


def run_loop(seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        NSRunLoop.currentRunLoop().runUntilDate_(
            NSDate.dateWithTimeIntervalSinceNow_(0.05)
        )


def uuid_str(obj):
    return str(obj.UUIDString()).upper()


def data_bytes(data):
    return bytes(data) if data is not None else b""


class Delegate(NSObject):
    def init(self):
        self = objc.super(Delegate, self).init()
        self.central = None
        self.peripheral = None
        self.pending = 0
        self.done = False
        self.connected_at = 0.0
        self.last_discovery_try = 0.0
        return self

    def centralManagerDidUpdateState_(self, central):
        print(f"central state={central.state()}", flush=True)
        if central.state() != CBManagerStatePoweredOn:
            return
        hid_uuid = CBUUID.UUIDWithString_(HID_SERVICE)
        connected = list(central.retrieveConnectedPeripheralsWithServices_([hid_uuid]) or [])
        print(f"connected HID candidates={len(connected)}", flush=True)
        for peripheral in connected:
            name = str(peripheral.name() or "")
            print(f"candidate name={name!r} uuid={peripheral.identifier()}", flush=True)
            if not TARGET_HINTS or any(hint in name for hint in TARGET_HINTS):
                self.use_peripheral(central, peripheral)
                return
        print("scanning for HID peripherals...", flush=True)
        central.scanForPeripheralsWithServices_options_([hid_uuid], None)

    def use_peripheral(self, central, peripheral):
        self.peripheral = peripheral
        peripheral.setDelegate_(self)
        central.stopScan()
        central.connectPeripheral_options_(peripheral, None)

    def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(
        self, central, peripheral, advertisementData, RSSI
    ):
        name = str(peripheral.name() or advertisementData.get("kCBAdvDataLocalName", "") or "")
        print(f"found name={name!r} rssi={RSSI}", flush=True)
        if any(hint in name for hint in TARGET_HINTS):
            self.use_peripheral(central, peripheral)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        print(f"connected name={peripheral.name()!r}", flush=True)
        self.connected_at = time.time()
        self.last_discovery_try = self.connected_at
        print(f"peripheral state={peripheral.state()} discovering HID service", flush=True)
        peripheral.discoverServices_([CBUUID.UUIDWithString_(HID_SERVICE)])

    def centralManager_didFailToConnectPeripheral_error_(self, central, peripheral, error):
        print(f"connect failed: {error}", flush=True)
        self.done = True

    def centralManager_didDisconnectPeripheral_error_(self, central, peripheral, error):
        print(f"disconnected: {error}", flush=True)
        self.done = True

    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            print(f"service error: {error}", flush=True)
            self.done = True
            return
        services = list(peripheral.services() or [])
        print(f"services={len(services)}", flush=True)
        for service in services:
            print(f"service {uuid_str(service.UUID())}", flush=True)
            peripheral.discoverCharacteristics_forService_(None, service)
            self.pending += 1

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        self.pending -= 1
        if error:
            print(f"char error {uuid_str(service.UUID())}: {error}", flush=True)
            return
        for ch in service.characteristics() or []:
            props = ch.properties()
            flags = []
            if props & CBCharacteristicPropertyRead:
                flags.append("read")
            if props & CBCharacteristicPropertyWrite:
                flags.append("write")
            if props & CBCharacteristicPropertyWriteWithoutResponse:
                flags.append("write-no-response")
            if props & 0x10:
                flags.append("notify")
            cu = uuid_str(ch.UUID())
            print(f"  char {cu} props=0x{props:x} {'/'.join(flags)}", flush=True)
            if cu in (REPORT_CHAR, REPORT_MAP_CHAR):
                if props & CBCharacteristicPropertyRead:
                    peripheral.readValueForCharacteristic_(ch)
                    self.pending += 1
                peripheral.discoverDescriptorsForCharacteristic_(ch)
                self.pending += 1
        if self.pending == 0:
            self.done = True

    def peripheral_didDiscoverDescriptorsForCharacteristic_error_(self, peripheral, characteristic, error):
        self.pending -= 1
        if error:
            print(f"descriptor error {uuid_str(characteristic.UUID())}: {error}", flush=True)
            return
        for desc in characteristic.descriptors() or []:
            du = uuid_str(desc.UUID())
            print(f"    descriptor {du}", flush=True)
            if du == REPORT_REFERENCE_DESC:
                peripheral.readValueForDescriptor_(desc)
                self.pending += 1
        if self.pending == 0:
            self.done = True

    def peripheral_didUpdateValueForCharacteristic_error_(self, peripheral, characteristic, error):
        self.pending -= 1
        cu = uuid_str(characteristic.UUID())
        if error:
            print(f"    value {cu} error: {error}", flush=True)
        else:
            raw = data_bytes(characteristic.value())
            shown = raw[:80].hex(" ")
            extra = "" if len(raw) <= 80 else f" ... ({len(raw)} bytes)"
            print(f"    value {cu} len={len(raw)} {shown}{extra}", flush=True)
        if self.pending == 0:
            self.done = True

    def peripheral_didUpdateValueForDescriptor_error_(self, peripheral, descriptor, error):
        self.pending -= 1
        du = uuid_str(descriptor.UUID())
        if error:
            print(f"    descriptor value {du} error: {error}", flush=True)
        else:
            raw = data_bytes(descriptor.value())
            print(f"    descriptor value {du} len={len(raw)} {raw.hex(' ')}", flush=True)
        if self.pending == 0:
            self.done = True


def main():
    parser = argparse.ArgumentParser(description="Inspect BLE HID reports for the 8BitDo controller.")
    parser.add_argument("--seconds", type=float, default=12.0)
    args = parser.parse_args()

    delegate = Delegate.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    delegate.central = central
    deadline = time.time() + args.seconds
    while time.time() < deadline and not delegate.done:
        if delegate.peripheral and delegate.connected_at:
            now = time.time()
            if now - delegate.last_discovery_try > 3.0 and delegate.pending == 0:
                delegate.last_discovery_try = now
                print(
                    f"retry discoverServices state={delegate.peripheral.state()} elapsed={now - delegate.connected_at:.1f}s",
                    flush=True,
                )
                delegate.peripheral.discoverServices_(None)
        run_loop(0.1)
    if delegate.peripheral:
        central.cancelPeripheralConnection_(delegate.peripheral)
    return 0 if delegate.peripheral else 2


if __name__ == "__main__":
    sys.exit(main())
