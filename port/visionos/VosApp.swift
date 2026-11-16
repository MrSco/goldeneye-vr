// Vision Pro, native: the app (docs/visionos-native.md).
//
// One scene: a full-immersion space whose CompositorLayer the game draws into
// through its own OpenXR runtime (port/visionos/vos_xr.mm). Info.plist makes
// the space the app's first scene, so the app opens straight into it, as the
// Quest app opens into VR. The layer renderer goes to vos_app.mm, which
// starts the game on a thread of its own; the compositor's frame loop is the
// game's xrWaitFrame from there on.
//
// Not called main.swift: Swift treats that name as top-level code, which
// clashes with @main (Midas's note).

import SwiftUI
import CompositorServices

struct VosLayerConfiguration: CompositorLayerConfiguration {
    func makeConfiguration(capabilities: LayerRenderer.Capabilities,
                           configuration: inout LayerRenderer.Configuration) {
        // sRGB: the game's colours reach the drawable unchanged (vos_xr.mm)
        configuration.colorFormat = .bgra8Unorm_srgb
        configuration.depthFormat = .depth32Float
        // no foveation for now: the composite draws each view whole
        configuration.isFoveationEnabled = false
        let layouts = capabilities.supportedLayouts(options: [])
        configuration.layout = layouts.contains(.dedicated) ? .dedicated : .layered
        NSLog("GoldenEyeVR: compositor layer: layout \(configuration.layout == .dedicated ? "dedicated" : "layered")")
    }
}

@main
struct GoldenEyeVRApp: App {
    var body: some Scene {
        // no id: the space the app launches into is the body's unnamed one
        // (Apple's full-immersion Metal template); with an id, SwiftUI found
        // no scene for the launch role and aborted (simulator, 2026-11-16)
        ImmersiveSpace {
            CompositorLayer(configuration: VosLayerConfiguration()) { layerRenderer in
                // on the main thread: vos_app.mm keeps a reference and starts
                // the game on its own thread
                gevrVosLayerReady(Unmanaged.passUnretained(layerRenderer).toOpaque())
            }
        }
        .immersionStyle(selection: .constant(.full), in: .full)
        // the Digital Crown's close and the system's home tap stay; nothing
        // else of the system's floats over the game
        .persistentSystemOverlays(.hidden)
    }
}
