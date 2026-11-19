// Vision Pro, native: the app (docs/visionos-native.md).
//
// The app opens straight into a full-immersion space whose CompositorLayer the
// game draws into through its own OpenXR runtime (port/visionos/vos_xr.mm), as
// the Quest app opens into VR (Info.plist: CPSceneSessionRoleImmersiveSpaceApplication).
// The layer renderer goes to vos_app.mm, which starts the game on a thread of
// its own; the compositor's frame loop is the game's xrWaitFrame from there on.
//
// The one window, beside the space, opened only when the game asks: the
// launcher's file picker (ROM, Bean import; vosPickerOpen), as Klepton's
// "kimport" window is, or a text field for its text boxes (vosKeyboardOpen):
// the system's keyboard comes up only for a text field in a window.
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
    @Published var pending = false   // the game is waiting for an answer

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
        if VosPanel.windowOpen {
            p.picking = true
            return
        }
        VosPanel.open { VosPicker.shared.finish(-1, nil) }
    }
}

// MARK: the window

enum VosPanel {
    static var windowOpen = false

    /// The window, beside the space (the app's only WindowGroup); failed runs
    /// on the main thread.
    static func open(failed: @escaping () -> Void) {
        let request = UISceneSessionActivationRequest(role: .windowApplication)
        UIApplication.shared.activateSceneSession(for: request) { error in
            NSLog("GoldenEyeVR: the window would not open: \(error)")
            DispatchQueue.main.async(execute: failed)
        }
    }
}

struct VosPanelView: View {
    @ObservedObject private var picker = VosPicker.shared
    @ObservedObject private var keyboard = VosKeyboard.shared
    @Environment(\.dismissWindow) private var dismissWindow

    var body: some View {
        Group {
            if picker.pending {
                VosPickerView()
            } else if keyboard.active {
                VosKeyboardView()
            } else {
                Color.clear   // closing
            }
        }
        .onAppear { VosPanel.windowOpen = true }
        .onDisappear {
            VosPanel.windowOpen = false
            picker.finish(0, nil)        // the window was closed: cancelled
            keyboard.active = false
        }
        .onChange(of: keyboard.active) { _, now in
            if !now && !picker.pending { dismissWindow(id: "panel") }
        }
    }
}

// MARK: the keyboard

final class VosKeyboard: ObservableObject {
    static let shared = VosKeyboard()
    @Published var active = false
    @Published var text = ""
    @Published var multiline = false
    @Published var opened = 0        // each open (a second box: the field takes the focus again)
    var synced = ""                  // the text the game's box holds
}

@_cdecl("vosKeyboardOpen")
func vosKeyboardOpen(_ text: UnsafePointer<CChar>?, _ multiline: Int32) {
    let t = text.map { String(cString: $0) } ?? ""
    DispatchQueue.main.async {
        let k = VosKeyboard.shared
        k.synced = t
        k.text = t
        k.multiline = multiline != 0
        k.active = true
        k.opened += 1
        if !VosPanel.windowOpen {
            VosPanel.open { VosKeyboard.shared.active = false }
        }
    }
}

@_cdecl("vosKeyboardClose")
func vosKeyboardClose() {
    DispatchQueue.main.async { VosKeyboard.shared.active = false }
}

struct VosKeyboardView: View {
    @ObservedObject private var keyboard = VosKeyboard.shared
    @FocusState private var focused: Bool

    var body: some View {
        VStack(spacing: 18) {
            field
                .textFieldStyle(.roundedBorder)
                .font(.title3)
                .focused($focused)
            Button("Done") { done() }
        }
        .padding(30)
        .onAppear {
            // the system's keyboard comes with the focus
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) { focused = true }
        }
        .onChange(of: keyboard.opened) { _, _ in focused = true }
        .onChange(of: keyboard.text) { _, now in
            guard now != keyboard.synced else { return }
            keyboard.synced = now
            now.withCString { vosKeyboardText($0) }
        }
    }

    @ViewBuilder private var field: some View {
        if keyboard.multiline {
            TextField("Type here", text: $keyboard.text, axis: .vertical)
                .lineLimit(3...6)
        } else {
            // names, codes, addresses: as typed
            TextField("Type here", text: $keyboard.text)
                .textInputAutocapitalization(.never)
                .autocorrectionDisabled()
                .onSubmit { done() }
        }
    }

    /// Return ends a one-line box (the game then puts the window away); a
    /// note keeps its box, and pointing at it again brings the window back.
    private func done() {
        if !keyboard.multiline { vosKeyboardSubmit() }
        keyboard.active = false
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
            if picker.pending { picker.picking = true }
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
        dismissWindow(id: "panel")
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

        WindowGroup(id: "panel") { VosPanelView() }
            .defaultSize(width: 620, height: 260)
            .windowResizability(.contentSize)
            .defaultLaunchBehavior(.suppressed)   // only when the game asks
            .restorationBehavior(.disabled)       // a request belongs to the run that made it
    }
}
