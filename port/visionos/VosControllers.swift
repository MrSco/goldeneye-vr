// Vision Pro, native: PlayStation VR2 Sense controllers as a Touch pair
// (docs/visionos-native.md, vos_input.mm).
//
// Ported from Klepton's visionos/Sources/KleptonControllers.swift (MIT,
// port/licenses/klepton.txt), which runs this same game on this headset:
//   - poses: ARKit's accessory tracking, the controller's .grip location with
//     the platform's .rendered correction, predicted to the frame's display
//     time (the anchor stream itself runs well below the display rate), then
//     Klepton's tuned turn from Apple's grip to a Touch grip: 5.037 degrees
//     (the PSVR2's own tilt) + -37 about X, offset (-0.005, 0.02, -0.02) m in
//     the grip's frame, x mirrored for the left hand;
//   - buttons: GameController's input state, read by element name ("Trigger",
//     "Grip", "Button A"/"B", "Thumbstick" as a d-pad, "Thumbstick Button",
//     "Button Menu"/"Options"); the hand from the vendor name's "(L)";
//   - rumble: each controller's CoreHaptics engine.
// The accessory provider's set is fixed when its session runs, so a session
// is run again whenever the paired set changes (Klepton found a run without
// controllers never picks one up later).

import Foundation
import ARKit
import GameController
import CoreHaptics
import QuartzCore
import simd

private func vosEulerXYZ(_ degrees: SIMD3<Float>) -> simd_quatf {
    let r = degrees * .pi / 180
    return simd_quatf(angle: r.x, axis: [1, 0, 0])
         * simd_quatf(angle: r.y, axis: [0, 1, 0])
         * simd_quatf(angle: r.z, axis: [0, 0, 1])
}

@available(visionOS 26.0, *)
final class VosControllers: @unchecked Sendable {
    static let shared = VosControllers()

    private let lock = NSLock()
    private var anchors: [Int: AccessoryAnchor] = [:]
    private var provider: AccessoryTrackingProvider?
    private var session: ARKitSession?
    private var tracked: [GCController] = []
    private var started = false
    private var rebuilding = false

    // Klepton's defaults (KLSenseTune, KleptonTuning)
    private static let psvr2Tilt: Float = 5.037
    private static let hiltPitch: Float = -37
    private static let gripTurn = vosEulerXYZ(SIMD3<Float>(psvr2Tilt + hiltPitch, 0, 0))
    private static let gripOffset = SIMD3<Float>(-0.005, 0.02, -0.02)
    private static let predictMax: Double = 0.050   // seconds ahead, at most

    static func spatial() -> [GCController] {
        GCController.controllers().filter { $0.productCategory == GCProductCategorySpatialController }
    }

    static func hand(of c: GCController) -> Int {
        (c.vendorName ?? "").hasSuffix("(L)") ? 0 : 1
    }

    /// From the game's thread, every frame: starts the tracking once, and runs
    /// it again when the paired set changes.
    func poll() {
        lock.lock()
        let first = !started
        started = true
        lock.unlock()
        if first {
            NotificationCenter.default.addObserver(forName: .GCControllerDidConnect, object: nil, queue: nil) { [weak self] _ in
                self?.rebuild()
            }
            NotificationCenter.default.addObserver(forName: .GCControllerDidDisconnect, object: nil, queue: nil) { [weak self] _ in
                self?.rebuild()
            }
            rebuild()
        }
    }

    private func rebuild() {
        lock.lock()
        let now = Self.spatial()
        if rebuilding || (now == tracked && session != nil) { lock.unlock(); return }
        rebuilding = true
        tracked = now
        lock.unlock()
        Task.detached { [self] in
            await run(now)
            lock.lock(); rebuilding = false; lock.unlock()
        }
    }

    private func run(_ controllers: [GCController]) async {
        guard AccessoryTrackingProvider.isSupported else {
            NSLog("GoldenEyeVR: accessory tracking is not supported here")
            return
        }
        var accessories: [Accessory] = []
        for c in controllers {
            do { accessories.append(try await Accessory(device: c)) }
            catch { NSLog("GoldenEyeVR: accessory for \(c.vendorName ?? "?") failed: \(error)") }
        }
        NSLog("GoldenEyeVR: spatial controllers: \(accessories.map { $0.name })")
        let p = AccessoryTrackingProvider(accessories: accessories)
        let s = ARKitSession()
        do {
            try await s.run([p])
        } catch {
            NSLog("GoldenEyeVR: accessory tracking did not start: \(error)")
            return
        }
        lock.lock()
        session?.stop()
        session = s
        provider = p
        anchors = [:]
        lock.unlock()
        for await update in p.anchorUpdates {
            let a = update.anchor
            let hand: Int
            switch a.accessory.inherentChirality {
            case .left: hand = 0
            default: hand = 1   // a controller that has not said: the right (ALVR, Klepton)
            }
            store(hand: hand, anchor: update.event == .removed ? nil : a)
        }
    }

    private func store(hand: Int, anchor: AccessoryAnchor?) {
        lock.lock()
        anchors[hand] = anchor
        lock.unlock()
    }

    /// This hand's grip at a display time (seconds, CACurrentMediaTime's clock).
    func grip(hand: Int, at time: Double) -> simd_float4x4? {
        lock.lock()
        let stored = anchors[hand]
        let p = provider
        lock.unlock()
        guard let stored, let p else { return nil }
        let now = CACurrentMediaTime()
        let ahead = min(max(time - now, 0), Self.predictMax)
        let a = p.predictAnchor(for: stored, at: now + ahead) ?? stored
        guard a.isTracked else { return nil }
        let m = a.accessory.locations.contains(.grip)
            ? a.coordinateSpace(for: .grip, correction: .rendered).ancestorFromSpaceTransformFloat().matrix
            : a.originFromAnchorTransform
        let raw = simd_quatf(m)
        let q = simd_normalize(raw * Self.gripTurn)
        let off = SIMD3<Float>(hand == 0 ? -Self.gripOffset.x : Self.gripOffset.x, Self.gripOffset.y, Self.gripOffset.z)
        let pos = SIMD3<Float>(m.columns.3.x, m.columns.3.y, m.columns.3.z) + raw.act(off)
        var out = simd_float4x4(q)
        out.columns.3 = SIMD4<Float>(pos, 1)
        return out
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

/// vos_input.mm, from the game's thread at xrSyncActions: both hands' state.
/// Returns how many Sense controllers are connected.
@_cdecl("vosSenseSync")
func vosSenseSync(_ displaySeconds: Double, _ out: UnsafeMutablePointer<VosSenseHand>) -> Int32 {
    for h in 0..<2 { out[h] = VosSenseHand() }
    guard #available(visionOS 26.0, *) else { return 0 }
    let ctl = VosControllers.shared
    ctl.poll()
    var count: Int32 = 0
    for c in VosControllers.spatial() {
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
        if let m = ctl.grip(hand: hand, at: displaySeconds) {
            st.tracked = 1
            withUnsafeMutableBytes(of: &st.grip) { raw in
                let f = raw.bindMemory(to: Float.self)
                for col in 0..<4 { for row in 0..<4 { f[col * 4 + row] = m[col][row] } }
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
