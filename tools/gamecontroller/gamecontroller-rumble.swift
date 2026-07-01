import CoreHaptics
import Foundation
import GameController

func describe(_ controller: GCController) {
    print("controller: \(controller.vendorName ?? "(unknown)")")
    print("  productCategory: \(controller.productCategory)")
    print("  extendedGamepad: \(controller.extendedGamepad != nil)")
    print("  microGamepad: \(controller.microGamepad != nil)")
    print("  haptics: \(controller.haptics != nil)")
}

GCController.startWirelessControllerDiscovery {}
RunLoop.current.run(until: Date().addingTimeInterval(2.0))

let controllers = GCController.controllers()
print("controllers found: \(controllers.count)")

guard let controller = controllers.first(where: {
    ($0.vendorName ?? "").localizedCaseInsensitiveContains("8BitDo")
}) ?? controllers.first else {
    print("no GameController devices found")
    exit(1)
}

describe(controller)

guard let haptics = controller.haptics else {
    print("no haptics object exposed by GameController")
    exit(2)
}

let localities: [GCHapticsLocality] = [.default, .leftHandle, .rightHandle]
let event = CHHapticEvent(
    eventType: .hapticContinuous,
    parameters: [
        CHHapticEventParameter(parameterID: .hapticIntensity, value: 1.0),
        CHHapticEventParameter(parameterID: .hapticSharpness, value: 0.3)
    ],
    relativeTime: 0,
    duration: 1.0
)
let pattern = try CHHapticPattern(events: [event], parameters: [])

var played = false
for locality in localities {
    do {
        print("trying haptics locality: \(locality.rawValue)")
        let engine = haptics.createEngine(withLocality: locality)
        try engine.start()
        let player = try engine.makePlayer(with: pattern)
        try player.start(atTime: CHHapticTimeImmediate)
        RunLoop.current.run(until: Date().addingTimeInterval(1.1))
        engine.stop(completionHandler: nil)
        played = true
    } catch {
        print("  failed: \(error)")
    }
}

exit(played ? 0 : 3)
