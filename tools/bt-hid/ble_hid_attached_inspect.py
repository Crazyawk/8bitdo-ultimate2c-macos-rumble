#!/usr/bin/env python3
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

DEFAULT_SERVICE = "1812"
REPORT_CHAR = "2A4D"
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
        self.pending = 0
        self.done = False
        self.peripheral = None
        return self

    def centralManagerDidUpdateState_(self, central):
        print(f"central state={central.state()}", flush=True)
        if central.state() != CBManagerStatePoweredOn:
            return
        service_uuid = CBUUID.UUIDWithString_(self.service_uuid)
        connected = list(central.retrieveConnectedPeripheralsWithServices_([service_uuid]) or [])
        print(f"already-connected {self.service_uuid} candidates={len(connected)}", flush=True)
        for peripheral in connected:
            name = str(peripheral.name() or "")
            print(f"candidate name={name!r} state={peripheral.state()} uuid={peripheral.identifier()}", flush=True)
            if any(hint in name for hint in TARGET_HINTS):
                self.peripheral = peripheral
                peripheral.setDelegate_(self)
                peripheral.discoverServices_([service_uuid])
                self.pending += 1
                return
        self.done = True

    def peripheral_didDiscoverServices_(self, peripheral, error):
        self.pending -= 1
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
        if self.pending == 0:
            self.done = True

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        self.pending -= 1
        if error:
            print(f"char error {uuid_str(service.UUID())}: {error}", flush=True)
        else:
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
                print(f"  char {uuid_str(ch.UUID())} props=0x{props:x} {'/'.join(flags)}", flush=True)
                if uuid_str(ch.UUID()) == REPORT_CHAR:
                    peripheral.discoverDescriptorsForCharacteristic_(ch)
                    self.pending += 1
                    if props & CBCharacteristicPropertyRead:
                        peripheral.readValueForCharacteristic_(ch)
                        self.pending += 1
        if self.pending == 0:
            self.done = True

    def peripheral_didDiscoverDescriptorsForCharacteristic_error_(self, peripheral, characteristic, error):
        self.pending -= 1
        if error:
            print(f"descriptor error {uuid_str(characteristic.UUID())}: {error}", flush=True)
        else:
            print(f"    descriptors for {uuid_str(characteristic.UUID())}", flush=True)
            for desc in characteristic.descriptors() or []:
                print(f"    descriptor {uuid_str(desc.UUID())}", flush=True)
                if uuid_str(desc.UUID()) == REPORT_REFERENCE_DESC:
                    peripheral.readValueForDescriptor_(desc)
                    self.pending += 1
        if self.pending == 0:
            self.done = True

    def peripheral_didUpdateValueForCharacteristic_error_(self, peripheral, characteristic, error):
        self.pending -= 1
        if error:
            print(f"    value {uuid_str(characteristic.UUID())} error: {error}", flush=True)
        else:
            raw = data_bytes(characteristic.value())
            print(f"    value {uuid_str(characteristic.UUID())} len={len(raw)} {raw[:64].hex(' ')}", flush=True)
        if self.pending == 0:
            self.done = True

    def peripheral_didUpdateValueForDescriptor_error_(self, peripheral, descriptor, error):
        self.pending -= 1
        if error:
            print(f"    descriptor value {uuid_str(descriptor.UUID())} error: {error}", flush=True)
        else:
            raw = data_bytes(descriptor.value())
            print(f"    descriptor value {uuid_str(descriptor.UUID())} len={len(raw)} {raw.hex(' ')}", flush=True)
        if self.pending == 0:
            self.done = True


def main():
    service_uuid = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SERVICE
    delegate = Delegate.alloc().init()
    delegate.service_uuid = service_uuid
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    deadline = time.time() + 8
    while time.time() < deadline and not delegate.done:
        run_loop(0.1)
    return 0 if delegate.peripheral else 2


if __name__ == "__main__":
    sys.exit(main())
