#!/usr/bin/env python3
import sys
import time

import objc
from Foundation import NSObject, NSRunLoop, NSDate
from CoreBluetooth import (
    CBCentralManager,
    CBCharacteristicPropertyWrite,
    CBCharacteristicPropertyWriteWithoutResponse,
    CBManagerStatePoweredOn,
)


TARGET_NAMES = ("8BitDo", "Ultimate 2C")


def run_for(seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        NSRunLoop.currentRunLoop().runUntilDate_(
            NSDate.dateWithTimeIntervalSinceNow_(0.1)
        )


def uuid_str(obj):
    return str(obj.UUIDString())


class Delegate(NSObject):
    def init(self):
        self = objc.super(Delegate, self).init()
        self.central = None
        self.target = None
        self.done = False
        return self

    def centralManagerDidUpdateState_(self, central):
        print(f"central state={central.state()}", flush=True)
        if central.state() == CBManagerStatePoweredOn:
            print("scanning...", flush=True)
            central.scanForPeripheralsWithServices_options_(None, None)

    def centralManager_didDiscoverPeripheral_advertisementData_RSSI_(
        self, central, peripheral, advertisementData, RSSI
    ):
        name = str(peripheral.name() or advertisementData.get("kCBAdvDataLocalName", "") or "")
        if name:
            print(f"found name={name!r} rssi={RSSI}", flush=True)
        if any(part in name for part in TARGET_NAMES):
            print(f"target found: {name!r} uuid={peripheral.identifier()}", flush=True)
            self.target = peripheral
            peripheral.setDelegate_(self)
            central.stopScan()
            central.connectPeripheral_options_(peripheral, None)

    def centralManager_didConnectPeripheral_(self, central, peripheral):
        print(f"connected: {peripheral.name()}", flush=True)
        peripheral.discoverServices_(None)

    def centralManager_didFailToConnectPeripheral_error_(self, central, peripheral, error):
        print(f"connect failed: {error}", flush=True)
        self.done = True

    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            print(f"service discovery error: {error}", flush=True)
            self.done = True
            return
        for service in peripheral.services() or []:
            print(f"service {uuid_str(service.UUID())}", flush=True)
            peripheral.discoverCharacteristics_forService_(None, service)

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        if error:
            print(f"characteristic discovery error {uuid_str(service.UUID())}: {error}", flush=True)
            return
        for ch in service.characteristics() or []:
            props = ch.properties()
            flags = []
            if props & CBCharacteristicPropertyWrite:
                flags.append("write")
            if props & CBCharacteristicPropertyWriteWithoutResponse:
                flags.append("write-no-response")
            if props & 0x02:
                flags.append("read")
            if props & 0x10:
                flags.append("notify")
            print(
                f"  char {uuid_str(ch.UUID())} props=0x{props:x} {'/'.join(flags)}",
                flush=True,
            )
        # Let all service callbacks arrive before exiting.
        self._last_seen = time.time()


def main():
    delegate = Delegate.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(delegate, None)
    delegate.central = central
    run_for(float(sys.argv[1]) if len(sys.argv) > 1 else 20.0)
    if delegate.target:
        central.cancelPeripheralConnection_(delegate.target)


if __name__ == "__main__":
    main()
