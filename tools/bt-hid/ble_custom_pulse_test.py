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


def run_loop(seconds):
    end = time.time() + seconds
    while time.time() < end:
        NSRunLoop.currentRunLoop().runUntilDate_(
            NSDate.dateWithTimeIntervalSinceNow_(0.02)
        )


def nsdata(payload):
    return NSData.dataWithBytes_length_(payload, len(payload))


def uuid_text(uuid):
    return str(uuid.UUIDString()).upper()


class Delegate(NSObject):
    def init(self):
        self = objc.super(Delegate, self).init()
        self.central = None
        self.peripheral = None
        self.characteristic = None
        self.ready = False
        self.disconnected = False
        return self

    def centralManagerDidUpdateState_(self, central):
        print(f"central state={central.state()}", flush=True)
        if central.state() != CBManagerStatePoweredOn:
            return
        service = CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)
        found = list(central.retrieveConnectedPeripheralsWithServices_([service]) or [])
        print(f"connected-service candidates={len(found)}", flush=True)
        if found:
            self.use_peripheral(central, found[0])
            return
        central.scanForPeripheralsWithServices_options_([service], None)
        print("scanning", flush=True)

    def use_peripheral(self, central, peripheral):
        self.peripheral = peripheral
        peripheral.setDelegate_(self)
        central.stopScan()
        central.connectPeripheral_options_(peripheral, None)

    def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(
        self, central, peripheral, advertisementData, RSSI
    ):
        name = str(peripheral.name() or advertisementData.get("kCBAdvDataLocalName", "") or "")
        print(f"found {name!r}", flush=True)
        if TARGET_NAME in name or "8BitDo" in name:
            self.use_peripheral(central, peripheral)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        print("connected; discovering custom service", flush=True)
        peripheral.discoverServices_([CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)])

    def centralManager_didDisconnectPeripheral_error_(self, central, peripheral, error):
        print(f"disconnected: {error}", flush=True)
        self.disconnected = True
        self.ready = False

    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            print(f"service error: {error}", flush=True)
            return
        for service in peripheral.services() or []:
            peripheral.discoverCharacteristics_forService_(None, service)

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        if error:
            print(f"char error: {error}", flush=True)
            return
        for characteristic in service.characteristics() or []:
            print(f"char {uuid_text(characteristic.UUID())} props=0x{characteristic.properties():x}", flush=True)
            if uuid_text(characteristic.UUID()).endswith(TARGET_CHAR_SUFFIX):
                self.characteristic = characteristic
                self.ready = True
                print("ready", flush=True)

    def sendPayload_(self, payload):
        self.peripheral.writeValue_forCharacteristic_type_(
            nsdata(payload), self.characteristic, CBCharacteristicWriteWithoutResponse
        )


def main():
    delegate = Delegate.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    delegate.central = central

    deadline = time.time() + 8.0
    while not delegate.ready and time.time() < deadline:
        run_loop(0.05)
    if not delegate.ready:
        print("not ready", flush=True)
        return 2

    print("stabilizing for 5 seconds before pulse test", flush=True)
    run_loop(5.0)

    on = bytes([0x05, 100, 100, 100, 100])
    off = bytes([0x05, 0, 0, 0, 0])
    for i in range(3):
        print(f"pulse {i + 1}/3 on", flush=True)
        delegate.sendPayload_(on)
        run_loop(0.22)
        print(f"pulse {i + 1}/3 off", flush=True)
        delegate.sendPayload_(off)
        run_loop(0.35)

    print("holding connection for 5 seconds after test", flush=True)
    run_loop(5.0)
    # Do not explicitly cancel the connection; let macOS tear down naturally when the process exits.
    return 0


if __name__ == "__main__":
    sys.exit(main())
