// Vision Pro, native: PlayStation VR2 Sense controllers as a Touch pair
// (docs/visionos-native.md, vos_input.mm).
//
// Ported from Klepton's visionos/Sources/KleptonControllers.swift (MIT,
// port/licenses/klepton.txt), which runs this same game on this headset:
//   - poses: not here. ARKit's accessory tracking runs in the head's own
//     session (vos_xr.mm vosAccessoryGrip, with Klepton's turn to a Touch
//     grip, Klepton's net turn for an OpenXR game);
//   - buttons: GameController's input state, read by element name ("Trigger",
//     "Grip", "Button A"/"B", "Thumbstick" as a d-pad, "Thumbstick Button",
//     "Button Menu"/"Options"); the hand from the vendor name's "(L)";
//   - rumble: each controller's CoreHaptics engine.

import Foundation
import GameController
import CoreHaptics

@available(visionOS 26.0, *)
final class VosControllers: @unchecked Sendable {
    static let shared = VosControllers()

    private let lock = NSLock()
    private var tracked: [GCController] = []

    static func spatial() -> [GCController] {
        GCController.controllers().filter { $0.productCategory == GCProductCategorySpatialController }
    }

    static func hand(of c: GCController) -> Int {
        (c.vendorName ?? "").hasSuffix("(L)") ? 0 : 1
    }

    func note(_ now: [GCController]) {
        lock.lock(); tracked = now; lock.unlock()
    }

    // MARK: rumble

    private var engines: [Int: CHHapticEngine] = [:]
    private let hapticsQueue = DispatchQueue(label: "gevr.haptics", qos: .userInitiated)
    private var hapticsBusy = [false, false]

    func rumble(hand: Int, amplitude: Float, seconds: Double) {
        lock.lock()
        if hapticsBusy[hand] { lock.unlock(); return }   // never block the frame on CoreHaptics
        hapticsBusy[hand] = true
        let c = tracked.first { Self.hand(of: $0) == hand }
        lock.unlock()
        hapticsQueue.async { [self] in
            defer { lock.lock(); hapticsBusy[hand] = false; lock.unlock() }
            guard let c, amplitude > 0, seconds > 0 else { return }
            if engines[hand] == nil {
                engines[hand] = c.haptics?.createEngine(withLocality: .default)
                try? engines[hand]?.start()
                engines[hand]?.resetHandler = { [weak self] in try? self?.engines[hand]?.start() }
            }
            guard let engine = engines[hand] else { return }
            let event = CHHapticEvent(eventType: .hapticContinuous, parameters: [
                CHHapticEventParameter(parameterID: .hapticIntensity, value: min(max(amplitude, 0), 1)),
                CHHapticEventParameter(parameterID: .hapticSharpness, value: 1.0),
            ], relativeTime: 0, duration: min(seconds, 1.0))
            do {
                let player = try engine.makePlayer(with: try CHHapticPattern(events: [event], parameters: []))
                try player.start(atTime: CHHapticTimeImmediate)
            } catch {
                engines[hand] = nil   // made again next time
            }
        }
    }
}

/// vos_input.mm, from the game's thread at xrSyncActions: both hands' buttons
/// and sticks (the poses are vos_xr.mm's). Returns how many are connected.
@_cdecl("vosSenseSync")
func vosSenseSync(_ displaySeconds: Double, _ out: UnsafeMutablePointer<VosSenseHand>) -> Int32 {
    for h in 0..<2 { out[h] = VosSenseHand() }
    guard #available(visionOS 26.0, *) else { return 0 }
    let ctl = VosControllers.shared
    let spatial = VosControllers.spatial()
    ctl.note(spatial)
    var count: Int32 = 0
    for c in spatial {
        let hand = VosControllers.hand(of: c)
        count += 1
        var st = VosSenseHand()
        st.connected = 1
        c.input.inputStateQueueDepth = 1
        if let s = c.input.nextInputState() {
            let b = s.buttons, dp = s.dpads
            func pressed(_ n: String) -> Bool { b[n]?.pressedInput.isPressed ?? false }
            func value(_ n: String) -> Float { b[n]?.pressedInput.value ?? 0 }
            func touched(_ n: String) -> Bool { b[n]?.touchedInput?.isTouched ?? false }
            st.trigger = value("Trigger")
            st.triggerTouch = touched("Trigger") ? 1 : 0
            st.squeeze = max(value("Grip"), pressed("Grip") ? 1 : 0)
            st.a = pressed("Button A") ? 1 : 0
            st.b = pressed("Button B") ? 1 : 0
            st.stickClick = pressed("Thumbstick Button") ? 1 : 0
            st.menu = (pressed("Button Menu") || pressed("Button Options") || pressed("Button Share")) ? 1 : 0
            if let d = dp["Thumbstick"] ?? dp[hand == 0 ? "Left Thumbstick" : "Right Thumbstick"] {
                st.stickX = d.xAxis.value
                st.stickY = d.yAxis.value
            }
        }
        out[hand] = st
    }
    return count
}

@_cdecl("vosSenseHaptic")
func vosSenseHaptic(_ hand: Int32, _ amplitude: Float, _ seconds: Double) {
    guard #available(visionOS 26.0, *), hand == 0 || hand == 1 else { return }
    VosControllers.shared.rumble(hand: Int(hand), amplitude: amplitude, seconds: seconds)
}
