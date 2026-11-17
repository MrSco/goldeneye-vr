// Vision Pro, native: the app (docs/visionos-native.md).
//
// The app opens straight into a full-immersion space whose CompositorLayer the
// game draws into through its own OpenXR runtime (port/visionos/vos_xr.mm), as
// the Quest app opens into VR (Info.plist: CPSceneSessionRoleImmersiveSpaceApplication).
// The layer renderer goes to vos_app.mm, which starts the game on a thread of
// its own; the compositor's frame loop is the game's xrWaitFrame from there on.
//
// The one window is the launcher's file picker (ROM, Bean import): opened
// only when the game asks (vosPickerOpen), beside the space, as Klepton's
// "kimport" window is.
//
// Not called main.swift: Swift treats that name as top-level code, which
// clashes with @main (Midas's note).

import SwiftUI
import UIKit
import CompositorServices
import UniformTypeIdentifiers

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

// MARK: the file picker

final class VosPicker: ObservableObject {
    static let shared = VosPicker()
    @Published var title = "Choose a file"
    @Published var picking = false
    var pending = false      // the game is waiting for an answer
    var windowOpen = false

    func finish(_ status: Int32, _ path: String?) {
        guard pending else { return }
        pending = false
        if let path { path.withCString { vosPickerDone(status, $0) } } else { vosPickerDone(status, nil) }
    }
}

@_cdecl("vosPickerOpen")
func vosPickerOpen(_ title: UnsafePointer<CChar>?) {
    let t = title.map { String(cString: $0) } ?? "Choose a file"
    DispatchQueue.main.async {
        let p = VosPicker.shared
        p.title = t
        p.pending = true
        if p.windowOpen {
            p.picking = true
            return
        }
        // the picker's window, beside the space (the app's only WindowGroup)
        let request = UISceneSessionActivationRequest(role: .windowApplication)
        UIApplication.shared.activateSceneSession(for: request) { error in
            NSLog("GoldenEyeVR: the picker's window would not open: \(error)")
            DispatchQueue.main.async { VosPicker.shared.finish(-1, nil) }
        }
    }
}

struct VosPickerView: View {
    @ObservedObject private var picker = VosPicker.shared
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        VStack(spacing: 18) {
            Text(picker.title).font(.title2)
            Text("Choose the file in the picker. GoldenEye VR keeps a copy.")
                .foregroundStyle(.secondary)
            HStack {
                Button("Show the picker") { picker.picking = true }
                Button("Cancel") { close(0, nil) }
            }
        }
        .padding(30)
        .fileImporter(isPresented: $picker.picking, allowedContentTypes: [.item]) { result in
            switch result {
            case .success(let url): close(1, copyIn(url))
            case .failure(let error):
                NSLog("GoldenEyeVR: picker: \(error)")
                close(0, nil)
            }
        }
        .onChange(of: picker.picking) { _, now in
            // closed without a choice (no callback comes then)
            if !now {
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    if picker.pending && !picker.picking { close(0, nil) }
                }
            }
        }
        .onAppear {
            picker.windowOpen = true
            if picker.pending { picker.picking = true }
        }
        .onDisappear {
            picker.windowOpen = false
            picker.finish(0, nil)   // the window was closed: cancelled
        }
    }

    /// The chosen file, copied where the game can read it (the caller copies it on).
    private func copyIn(_ url: URL) -> String? {
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let dest = FileManager.default.temporaryDirectory.appendingPathComponent(url.lastPathComponent)
        try? FileManager.default.removeItem(at: dest)
        do {
            try FileManager.default.copyItem(at: url, to: dest)
            return dest.path
        } catch {
            NSLog("GoldenEyeVR: picker: could not copy \(url.lastPathComponent): \(error)")
            return nil
        }
    }

    private func close(_ status: Int32, _ path: String?) {
        picker.finish(status == 1 && path == nil ? -1 : status, path)
        dismissWindow(id: "picker")
    }
}

@main
struct GoldenEyeVRApp: App {
    var body: some Scene {
        // no id: the space the app launches into is the body's unnamed one
        // (Apple's full-immersion Metal template)
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
        // the player's own hands and arms not drawn over the game's (tester,
        // build 9); Klepton sets the same on its space
        .upperLimbVisibility(.hidden)

        WindowGroup(id: "picker") { VosPickerView() }
            .defaultSize(width: 620, height: 260)
            .windowResizability(.contentSize)
            .defaultLaunchBehavior(.suppressed)   // only when the game asks
            .restorationBehavior(.disabled)       // a request belongs to the run that made it
    }
}
