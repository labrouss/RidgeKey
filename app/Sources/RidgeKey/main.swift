// RidgeKey - entry point.
// SPDX-License-Identifier: MIT
import SwiftUI

struct RidgeKeyApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup("RidgeKey") {
            ContentView()
                .environmentObject(model)
                .frame(minWidth: 720, minHeight: 480)
        }
        .windowResizability(.contentMinSize)
    }
}

// `RidgeKey --version` prints the version without opening a window (used by CI).
if CommandLine.arguments.contains("--version") {
    let v = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    print("RidgeKey \(v)")
    exit(0)
}
RidgeKeyApp.main()
