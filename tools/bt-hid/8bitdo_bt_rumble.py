#!/usr/bin/env python3
import argparse
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
            NSDate.dateWithTimeIntervalSinceNow_(0.03)
        )


def nsdata(payload):
    return NSData.dataWithBytes_length_(payload, len(payload))


def uuid_text(uuid):
    return str(uuid.UUIDString()).upper()


class RumbleDelegate(NSObject):
    def init(self):
        self = objc.super(RumbleDelegate, self).init()
        self.central = None
        self.peripheral = None
        self.characteristic = None
        self.ready = False
        return self

    def centralManagerDidUpdateState_(self, central):
        if central.state() != CBManagerStatePoweredOn:
            print(f"Bluetooth not ready, CoreBluetooth state={central.state()}", file=sys.stderr)
            return

        services = [CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)]
        for peripheral in central.retrieveConnectedPeripheralsWithServices_(services):
            name = str(peripheral.name() or "")
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
        if TARGET_NAME in name or "8BitDo" in name:
            self.peripheral = peripheral
            peripheral.setDelegate_(self)
            central.stopScan()
            central.connectPeripheral_options_(peripheral, None)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        peripheral.discoverServices_([CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)])

    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            print(f"Service discovery failed: {error}", file=sys.stderr)
            return
        for service in peripheral.services() or []:
            peripheral.discoverCharacteristics_forService_(None, service)

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        if error:
            print(f"Characteristic discovery failed: {error}", file=sys.stderr)
            return
        for characteristic in service.characteristics() or []:
            if uuid_text(characteristic.UUID()).endswith(TARGET_CHAR_SUFFIX):
                self.characteristic = characteristic
                self.ready = True


def packet(strength):
    value = max(0, min(100, int(strength)))
    return bytes([0x05, value, value, value, value])


def main():
    parser = argparse.ArgumentParser(description="Rumble an 8BitDo Ultimate 2C Wireless over Bluetooth.")
    parser.add_argument("strength", nargs="?", type=int, default=64, help="0-100 motor strength")
    parser.add_argument("duration", nargs="?", type=float, default=0.35, help="seconds to rumble")
    parser.add_argument("--connect-timeout", type=float, default=6.0)
    args = parser.parse_args()

    delegate = RumbleDelegate.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    delegate.central = central

    deadline = time.time() + args.connect_timeout
    while not delegate.ready and time.time() < deadline:
        run_loop(0.05)

    if not delegate.ready:
        print("Could not find the connected 8BitDo BLE rumble characteristic.", file=sys.stderr)
        return 2

    on = packet(args.strength)
    off = packet(0)
    delegate.peripheral.writeValue_forCharacteristic_type_(
        nsdata(on), delegate.characteristic, CBCharacteristicWriteWithoutResponse
    )
    run_loop(max(0.0, args.duration))
    delegate.peripheral.writeValue_forCharacteristic_type_(
        nsdata(off), delegate.characteristic, CBCharacteristicWriteWithoutResponse
    )
    run_loop(0.1)

    if delegate.peripheral:
        central.cancelPeripheralConnection_(delegate.peripheral)
    print(f"sent rumble strength={max(0, min(100, args.strength))} duration={args.duration:.2f}s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
