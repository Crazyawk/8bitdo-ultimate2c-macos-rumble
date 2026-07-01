#!/usr/bin/env python3
import argparse
import socket
import sys
import time

from Foundation import NSDate, NSRunLoop
from GameController import (
    GCController,
    GCHapticsLocalityDefault,
    GCHapticsLocalityLeftHandle,
    GCHapticsLocalityRightHandle,
)
from CoreHaptics import (
    CHHapticEngine,
    CHHapticEvent,
    CHHapticEventParameter,
    CHHapticEventParameterIDHapticIntensity,
    CHHapticEventParameterIDHapticSharpness,
    CHHapticEventTypeHapticContinuous,
    CHHapticEventTypeHapticTransient,
    CHHapticPattern,
)


DEFAULT_PORT = 39533
TARGET_NAME = "8BitDo"


def run_loop(seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        NSRunLoop.currentRunLoop().runUntilDate_(
            NSDate.dateWithTimeIntervalSinceNow_(0.02)
        )


def unwrap_error_result(result):
    if isinstance(result, tuple):
        value = result[0]
        error = result[1] if len(result) > 1 else None
        return value, error
    return result, None


def clamp(value, lo, hi):
    return max(lo, min(hi, value))


class GCHapticsBridge:
    def __init__(self, target_name=TARGET_NAME):
        self.target_name = target_name.lower()
        self.controller = None
        self.engines = []
        self.last_refresh = 0.0

    def start_discovery(self):
        GCController.startWirelessControllerDiscoveryWithCompletionHandler_(None)
        run_loop(0.2)

    def describe_controllers(self):
        controllers = list(GCController.controllers())
        print(f"GameController controllers={len(controllers)}", flush=True)
        for i, controller in enumerate(controllers):
            name = str(controller.vendorName() or "(unknown)")
            category = str(controller.productCategory() or "")
            haptics = controller.haptics()
            localities = list(haptics.supportedLocalities()) if haptics else []
            print(
                f"[{i}] {name} category={category} "
                f"haptics={'yes' if haptics else 'no'} localities={localities}",
                flush=True,
            )

    def refresh(self, force=False):
        now = time.time()
        if not force and now - self.last_refresh < 1.0 and self.engines:
            return bool(self.engines)
        self.last_refresh = now

        controllers = list(GCController.controllers())
        chosen = None
        for controller in controllers:
            name = str(controller.vendorName() or "")
            if self.target_name in name.lower():
                chosen = controller
                break
        if chosen is None and controllers:
            chosen = controllers[0]

        if chosen is self.controller and self.engines:
            return True

        self.stop()
        self.controller = chosen
        if not chosen:
            return False

        name = str(chosen.vendorName() or "(unknown)")
        haptics = chosen.haptics()
        if not haptics:
            print(f"controller {name} exposes no GameController haptics", flush=True)
            return False

        supported = list(haptics.supportedLocalities() or [])
        preferred = [
            GCHapticsLocalityLeftHandle,
            GCHapticsLocalityRightHandle,
            GCHapticsLocalityDefault,
        ]
        localities = [loc for loc in preferred if loc in supported]
        if not localities:
            localities = supported[:1]

        for locality in localities:
            try:
                engine = haptics.createEngineWithLocality_(locality)
                ok, error = unwrap_error_result(engine.startAndReturnError_(None))
                if not ok:
                    print(f"haptic engine start failed for {locality}: {error}", flush=True)
                    continue
                self.engines.append((locality, engine))
            except Exception as exc:
                print(f"haptic engine failed for {locality}: {exc}", flush=True)

        if self.engines:
            localities_text = [str(locality) for locality, _ in self.engines]
            print(f"ready: {name} localities={localities_text}", flush=True)
        return bool(self.engines)

    def make_pattern(self, strength, duration_ms):
        strength = clamp(float(strength), 0.0, 1.0)
        duration = clamp(float(duration_ms) / 1000.0, 0.02, 10.0)
        sharpness = 0.35 + 0.45 * strength
        params = [
            CHHapticEventParameter.alloc().initWithParameterID_value_(
                CHHapticEventParameterIDHapticIntensity, strength
            ),
            CHHapticEventParameter.alloc().initWithParameterID_value_(
                CHHapticEventParameterIDHapticSharpness, sharpness
            ),
        ]
        event_type = (
            CHHapticEventTypeHapticTransient
            if duration <= 0.08
            else CHHapticEventTypeHapticContinuous
        )
        if event_type == CHHapticEventTypeHapticTransient:
            event = CHHapticEvent.alloc().initWithEventType_parameters_relativeTime_(
                event_type, params, 0.0
            )
        else:
            event = CHHapticEvent.alloc().initWithEventType_parameters_relativeTime_duration_(
                event_type, params, 0.0, duration
            )
        pattern, error = unwrap_error_result(
            CHHapticPattern.alloc().initWithEvents_parameters_error_([event], [], None)
        )
        if not pattern:
            raise RuntimeError(f"pattern creation failed: {error}")
        return pattern

    def rumble(self, strength_percent, duration_ms):
        strength_percent = int(clamp(int(strength_percent), 0, 100))
        duration_ms = int(clamp(int(duration_ms), 0, 10000))
        if strength_percent <= 0 or duration_ms <= 0:
            self.stop_players()
            print("rumble off", flush=True)
            return True
        if not self.refresh():
            print("no haptic-capable GameController ready", flush=True)
            return False

        pattern = self.make_pattern(strength_percent / 100.0, duration_ms)
        played = False
        for locality, engine in list(self.engines):
            try:
                player, error = unwrap_error_result(
                    engine.createPlayerWithPattern_error_(pattern, None)
                )
                if not player:
                    print(f"player creation failed for {locality}: {error}", flush=True)
                    continue
                ok, error = unwrap_error_result(player.startAtTime_error_(0, None))
                if not ok:
                    print(f"player start failed for {locality}: {error}", flush=True)
                    continue
                played = True
            except Exception as exc:
                print(f"rumble failed for {locality}: {exc}", flush=True)
        if played:
            print(
                f"rumble strength={strength_percent} duration_ms={duration_ms}",
                flush=True,
            )
        return played

    def stop_players(self):
        # CoreHaptics patterns are duration-bounded; stopping engines is too disruptive.
        pass

    def stop(self):
        for _, engine in self.engines:
            try:
                engine.stopWithCompletionHandler_(None)
            except Exception:
                pass
        self.engines = []


def parse_request(text):
    parts = text.replace(",", " ").split()
    if not parts:
        return None
    strength = int(float(parts[0]))
    duration = int(float(parts[1])) if len(parts) > 1 else 250
    return strength, duration


def daemon(args):
    bridge = GCHapticsBridge(args.target)
    bridge.start_discovery()
    bridge.describe_controllers()
    bridge.refresh(force=True)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))
    sock.setblocking(False)
    print(f"listening on udp://{args.host}:{args.port}", flush=True)

    try:
        while True:
            run_loop(0.02)
            while True:
                try:
                    data, _ = sock.recvfrom(64)
                except BlockingIOError:
                    break
                try:
                    request = parse_request(data.decode("ascii", "ignore").strip())
                except ValueError:
                    continue
                if request:
                    bridge.rumble(*request)
            bridge.refresh()
    except KeyboardInterrupt:
        bridge.stop()
        print("stopped", flush=True)
        return 0


def once(args):
    bridge = GCHapticsBridge(args.target)
    bridge.start_discovery()
    run_loop(args.discovery_seconds)
    bridge.describe_controllers()
    ok = bridge.rumble(args.strength, args.duration_ms)
    run_loop((args.duration_ms / 1000.0) + 0.4)
    bridge.stop()
    return 0 if ok else 2


def main():
    parser = argparse.ArgumentParser(
        description="Local UDP-to-GameController/CoreHaptics rumble bridge."
    )
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--target", default=TARGET_NAME)
    parser.add_argument("--once", action="store_true")
    parser.add_argument("--strength", type=int, default=70)
    parser.add_argument("--duration-ms", type=int, default=500)
    parser.add_argument("--discovery-seconds", type=float, default=3.0)
    args = parser.parse_args()
    return once(args) if args.once else daemon(args)


if __name__ == "__main__":
    sys.exit(main())
