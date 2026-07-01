#!/usr/bin/env python3
import argparse
import socket
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
            NSDate.dateWithTimeIntervalSinceNow_(0.01)
        )


def uuid_text(uuid):
    return str(uuid.UUIDString()).upper()


def nsdata(payload):
    return NSData.dataWithBytes_length_(payload, len(payload))


class BLEBridge(NSObject):
    def init(self):
        self = objc.super(BLEBridge, self).init()
        self.central = None
        self.peripheral = None
        self.characteristic = None
        self.ready = False
        self.pending = None
        self.off_at = 0.0
        self.last_strength = 0
        return self

    def centralManagerDidUpdateState_(self, central):
        if central.state() != CBManagerStatePoweredOn:
            print(f"Bluetooth state={central.state()}, waiting", flush=True)
            return

        service = CBUUID.UUIDWithString_(TARGET_SERVICE_UUID)
        for peripheral in central.retrieveConnectedPeripheralsWithServices_([service]):
            name = str(peripheral.name() or "")
            if TARGET_NAME in name or "8BitDo" in name:
                self.peripheral = peripheral
                peripheral.setDelegate_(self)
                central.connectPeripheral_options_(peripheral, None)
                return
        central.scanForPeripheralsWithServices_options_([service], None)

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

    def centralManager_didDisconnectPeripheral_error_(self, central, peripheral, error):
        self.ready = False
        self.characteristic = None
        central.connectPeripheral_options_(peripheral, None)

    def peripheral_didDiscoverServices_(self, peripheral, error):
        if error:
            print(f"service discovery failed: {error}", file=sys.stderr, flush=True)
            return
        for service in peripheral.services() or []:
            peripheral.discoverCharacteristics_forService_(None, service)

    def peripheral_didDiscoverCharacteristicsForService_error_(self, peripheral, service, error):
        if error:
            print(f"characteristic discovery failed: {error}", file=sys.stderr, flush=True)
            return
        for characteristic in service.characteristics() or []:
            if uuid_text(characteristic.UUID()).endswith(TARGET_CHAR_SUFFIX):
                self.characteristic = characteristic
                self.ready = True
                print("8BitDo BLE rumble bridge ready", flush=True)
                self.flush_pending()

    def sendRumble_(self, strength):
        if not self.ready or not self.peripheral or not self.characteristic:
            return False
        value = max(0, min(100, int(strength)))
        payload = bytes([0x05, value, value, value, value])
        self.peripheral.writeValue_forCharacteristic_type_(
            nsdata(payload), self.characteristic, CBCharacteristicWriteWithoutResponse
        )
        return True

    def request_rumble(self, strength, duration_ms):
        strength = max(0, min(100, int(strength)))
        duration_ms = max(0, min(10000, int(duration_ms)))
        if self.sendRumble_(strength):
            self.mark_sent(strength, duration_ms)
            print(f"rumble strength={strength} duration_ms={duration_ms}", flush=True)
            return True
        if strength == 0 and self.pending and self.pending[0] > 0:
            print("kept queued rumble through pending off", flush=True)
            return False
        self.pending = (strength, duration_ms)
        print(f"queued strength={strength} duration_ms={duration_ms}", flush=True)
        return False

    def mark_sent(self, strength, duration_ms):
        if strength:
            self.last_strength = strength
            self.off_at = time.time() + (duration_ms / 1000.0)
        else:
            self.last_strength = 0
            self.off_at = 0.0

    def flush_pending(self):
        if not self.pending:
            return
        strength, duration_ms = self.pending
        if self.sendRumble_(strength):
            self.pending = None
            self.mark_sent(strength, duration_ms)
            print(f"rumble strength={strength} duration_ms={duration_ms}", flush=True)

    def tick(self):
        if self.ready:
            self.flush_pending()
        if self.last_strength and self.off_at and time.time() >= self.off_at:
            if self.sendRumble_(0):
                self.last_strength = 0
                self.off_at = 0.0


def main():
    parser = argparse.ArgumentParser(description="Local UDP-to-8BitDo-BLE rumble bridge.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=39532)
    args = parser.parse_args()

    bridge = BLEBridge.alloc().init()
    central = CBCentralManager.alloc().initWithDelegate_queue_(bridge, None)
    bridge.central = central

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    sock.setblocking(False)
    print(f"listening on udp://{args.host}:{args.port}", flush=True)

    try:
        while True:
            run_loop(0.01)
            while True:
                try:
                    data, _ = sock.recvfrom(64)
                except BlockingIOError:
                    break
                text = data.decode("ascii", "ignore").strip()
                if not text:
                    continue
                parts = text.replace(",", " ").split()
                try:
                    strength = int(float(parts[0]))
                    duration_ms = int(float(parts[1])) if len(parts) > 1 else 250
                except ValueError:
                    continue
                strength = max(0, min(100, strength))
                duration_ms = max(0, min(10000, duration_ms))
                bridge.request_rumble(strength, duration_ms)
            bridge.tick()
    except KeyboardInterrupt:
        bridge.sendRumble_(0)
        print("stopped", flush=True)
        return 0


if __name__ == "__main__":
    sys.exit(main())
