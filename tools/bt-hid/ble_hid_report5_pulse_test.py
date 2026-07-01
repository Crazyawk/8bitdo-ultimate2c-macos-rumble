#!/usr/bin/env python3
import sys
import time

import objc
from Foundation import NSDate, NSData, NSObject, NSRunLoop
from CoreBluetooth import (
    CBCentralManager,
    CBUUID,
    CBCharacteristicPropertyRead,
    CBCharacteristicPropertyWrite,
    CBCharacteristicPropertyWriteWithoutResponse,
    CBCharacteristicWriteWithResponse,
    CBCharacteristicWriteWithoutResponse,
    CBManagerStatePoweredOn,
)

TARGET_NAME = "8BitDo Ultimate 2C Wireless"
HID_SERVICE = "1812"
VISIBLE_SERVICE = "00010203-0405-0607-0809-0A0B0C0D1912"
REPORT_CHAR = "2A4D"
REPORT_REFERENCE_DESC = "2908"


def run_loop(seconds):
    end = time.time() + seconds
    while time.time() < end:
        NSRunLoop.currentRunLoop().runUntilDate_(
            NSDate.dateWithTimeIntervalSinceNow_(0.02)
        )


def uuid_text(uuid):
    return str(uuid.UUIDString()).upper()


def nsdata(payload):
    return NSData.dataWithBytes_length_(payload, len(payload))


def data_bytes(data):
    return bytes(data) if data is not None else b""


class Delegate(NSObject):
    def init(self):
        self = objc.super(Delegate, self).init()
        self.central = None
        self.peripheral = None
        self.pending = 0
        self.ready = False
        self.done_discovery = False
        self.report_candidates = []
        self.target_characteristic = None
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
            print(f"candidate {name!r} state={peripheral.state()} uuid={peripheral.identifier()}", flush=True)
            if TARGET_NAME in name or "8BitDo" in name:
                self.usePeripheral_(peripheral)
                return
        visible_uuid = CBUUID.UUIDWithString_(VISIBLE_SERVICE)
        visible = list(central.retrieveConnectedPeripheralsWithServices_([visible_uuid]) or [])
        print(f"connected visible-service candidates={len(visible)}", flush=True)
        for peripheral in visible:
            name = str(peripheral.name() or "")
            print(f"visible candidate {name!r} state={peripheral.state()} uuid={peripheral.identifier()}", flush=True)
            if TARGET_NAME in name or "8BitDo" in name:
                self.usePeripheral_(peripheral)
                return
        central.scanForPeripheralsWithServices_options_([visible_uuid], None)
        print("scanning visible service", flush=True)

    def usePeripheral_(self, peripheral):
        self.peripheral = peripheral
        peripheral.setDelegate_(self)
        self.central.stopScan()
        self.central.connectPeripheral_options_(peripheral, None)

    def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(
        self, central, peripheral, advertisementData, RSSI
    ):
        name = str(peripheral.name() or advertisementData.get("kCBAdvDataLocalName", "") or "")
        print(f"found {name!r} rssi={RSSI}", flush=True)
        if TARGET_NAME in name or "8BitDo" in name:
            self.usePeripheral_(peripheral)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        print("connected; discovering HID service", flush=True)
        peripheral.discoverServices_([CBUUID.UUIDWithString_(HID_SERVICE)])
        self.pending += 1

    def centralManager_didDisconnectPeripheral_error_(self, central, peripheral, error):
        print(f"disconnected: {error}", flush=True)

    def peripheral_didDiscoverServices_(self, peripheral, error):
        self.pending -= 1
        if error:
            print(f"service error: {error}", flush=True)
            self.done_discovery = True
            return
        for service in peripheral.services() or []:
            print(f"service {uuid_text(service.UUID())}", flush=True)
            peripheral.discoverCharacteristics_forService_(None, service)
            self.pending += 1
        if self.pending == 0:
            self.done_discovery = True

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        self.pending -= 1
        if error:
            print(f"char error: {error}", flush=True)
        else:
            for characteristic in service.characteristics() or []:
                cu = uuid_text(characteristic.UUID())
                props = characteristic.properties()
                flags = []
                if props & CBCharacteristicPropertyRead:
                    flags.append("read")
                if props & CBCharacteristicPropertyWrite:
                    flags.append("write")
                if props & CBCharacteristicPropertyWriteWithoutResponse:
                    flags.append("write-no-response")
                if props & 0x10:
                    flags.append("notify")
                print(f"  char {cu} props=0x{props:x} {'/'.join(flags)}", flush=True)
                if cu == REPORT_CHAR:
                    peripheral.discoverDescriptorsForCharacteristic_(characteristic)
                    self.pending += 1
        if self.pending == 0:
            self.done_discovery = True

    def peripheral_didDiscoverDescriptorsForCharacteristic_error_(self, peripheral, characteristic, error):
        self.pending -= 1
        if error:
            print(f"descriptor error: {error}", flush=True)
        else:
            for desc in characteristic.descriptors() or []:
                du = uuid_text(desc.UUID())
                print(f"    descriptor {du}", flush=True)
                if du == REPORT_REFERENCE_DESC:
                    self.report_candidates.append([characteristic, desc, None])
                    peripheral.readValueForDescriptor_(desc)
                    self.pending += 1
        if self.pending == 0:
            self.done_discovery = True

    def peripheral_didUpdateValueForDescriptor_error_(self, peripheral, descriptor, error):
        self.pending -= 1
        raw = b""
        if error:
            print(f"    descriptor value error: {error}", flush=True)
        else:
            raw = data_bytes(descriptor.value())
            print(f"    descriptor value {uuid_text(descriptor.UUID())} len={len(raw)} {raw.hex(' ')}", flush=True)
        for candidate in self.report_candidates:
            if candidate[1] is descriptor:
                candidate[2] = raw
        if raw == b"\x05\x02":
            self.target_characteristic = next(c[0] for c in self.report_candidates if c[1] is descriptor)
            print("selected report 5 output characteristic", flush=True)
        if self.pending == 0:
            self.done_discovery = True

    def peripheral_didWriteValueForCharacteristic_error_(self, peripheral, characteristic, error):
        if error:
            print(f"write callback error: {error}", flush=True)
        else:
            print("write callback ok", flush=True)

    def sendPayload_withResponse_(self, payload, with_response):
        write_type = CBCharacteristicWriteWithResponse if with_response else CBCharacteristicWriteWithoutResponse
        self.peripheral.writeValue_forCharacteristic_type_(
            nsdata(payload), self.target_characteristic, write_type
        )


def main():
    delegate = Delegate.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    delegate.central = central

    deadline = time.time() + 12.0
    while not delegate.done_discovery and time.time() < deadline:
        run_loop(0.05)

    if not delegate.target_characteristic:
        print("report 5 output characteristic not found", flush=True)
        return 2

    print("stabilizing for 5 seconds before HID pulse test", flush=True)
    run_loop(5.0)

    on_raw = bytes([100, 100, 100, 100])
    off_raw = bytes([0, 0, 0, 0])
    on_id = bytes([5, 100, 100, 100, 100])
    off_id = bytes([5, 0, 0, 0, 0])

    tests = [
        ("raw/no-response", on_raw, off_raw, False),
        ("id/no-response", on_id, off_id, False),
        ("raw/with-response", on_raw, off_raw, True),
    ]
    for label, on, off, with_response in tests:
        print(f"test {label} on", flush=True)
        delegate.sendPayload_withResponse_(on, with_response)
        run_loop(0.35)
        print(f"test {label} off", flush=True)
        delegate.sendPayload_withResponse_(off, with_response)
        run_loop(0.7)

    print("holding connection for 3 seconds after test", flush=True)
    run_loop(3.0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
