import Foundation
import GameController
import CoreHaptics

func pumpRunLoop(_ seconds: TimeInterval) {
    let until = Date().addingTimeInterval(seconds)
    while Date() < until {
        RunLoop.current.run(mode: .default, before: Date().addingTimeInterval(0.05))
    }
}

print("Starting wireless discovery...")
GCController.startWirelessControllerDiscovery {
    print("Discovery completion handler fired.")
}
pumpRunLoop(2.0)

let controllers = GCController.controllers()
print("controllers=\(controllers.count)")
for controller in controllers {
    print("controller vendor=\(controller.vendorName ?? "(null)") product=\(controller.productCategory) attached=\(controller.isAttachedToDevice)")
    print("  haptics=\(controller.haptics == nil ? "no" : "yes")")
    if let haptics = controller.haptics {
        let names = haptics.supportedLocalities.map { $0.rawValue }.sorted()
        print("  localities=\(names.joined(separator: ","))")
    }
}

let target = controllers.first {
    ($0.vendorName ?? "").localizedCaseInsensitiveContains("8BitDo") ||
    $0.productCategory.localizedCaseInsensitiveContains("8BitDo")
} ?? controllers.first

guard let controller = target else {
    fputs("No GameController device found.\n", stderr)
    exit(1)
}
guard let haptics = controller.haptics else {
    fputs("Selected controller has no GameController haptics object.\n", stderr)
    exit(2)
}
guard let engine = haptics.createEngine(withLocality: .default) else {
    fputs("createEngine returned nil.\n", stderr)
    exit(3)
}

do {
    try engine.start()
    let event = CHHapticEvent(
        eventType: .hapticContinuous,
        parameters: [
            CHHapticEventParameter(parameterID: .hapticIntensity, value: 1.0),
            CHHapticEventParameter(parameterID: .hapticSharpness, value: 0.5)
        ],
        relativeTime: 0,
        duration: 0.7
    )
    let pattern = try CHHapticPattern(events: [event], parameters: [])
    let player = try engine.makePlayer(with: pattern)
    print("Playing haptic pattern now...")
    try player.start(atTime: CHHapticTimeImmediate)
    pumpRunLoop(1.0)
    engine.stop()
    print("Done.")
} catch {
    fputs("Haptics failed: \(error)\n", stderr)
    exit(4)
}
