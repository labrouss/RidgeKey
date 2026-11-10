// RidgeKey - entry point.
// SPDX-License-Identifier: MIT
import AppKit
import SwiftUI

struct RidgeKeyApp: App {
    @StateObject private var model = AppModel()
    @StateObject private var updater = Updater()

    var body: some Scene {
        WindowGroup("RidgeKey") {
            ContentView()
                .environmentObject(model)
                .environmentObject(updater)
                .frame(minWidth: 720, minHeight: 480)
        }
        .windowResizability(.contentMinSize)
        .commands {
            CommandGroup(after: .appInfo) {
                Button("Check for Updates…") {
                    updater.showAbout = true
                    Task { await updater.check() }
                }
                .disabled(updater.busy)
            }
            CommandGroup(replacing: .help) {
                Button("RidgeKey Help") { NSWorkspace.shared.open(RK.docs) }
                    .keyboardShortcut("?", modifiers: .command)
                Button("Install & Upgrade") { NSWorkspace.shared.open(RK.docs("install")) }
                Button("Updates") { NSWorkspace.shared.open(RK.docs("install#updates")) }
                Button("Troubleshooting") { NSWorkspace.shared.open(RK.docs("troubleshooting")) }
                Divider()
                Button("Report an Issue…") { NSWorkspace.shared.open(RK.newIssue) }
                Button("RidgeKey on GitHub") { NSWorkspace.shared.open(RK.repo) }
            }
        }
    }
}

// `RidgeKey --version` prints the version without opening a window (used by CI).
if CommandLine.arguments.contains("--version") {
    let v = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    print("RidgeKey \(v)")
    exit(0)
}
RidgeKeyApp.main()
