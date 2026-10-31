// swift-tools-version:5.9
// RidgeKey settings app (SwiftUI). Build: scripts/build-app.sh (works with Xcode or just the
// Command Line Tools). Talks to ridgekeyd over XPC like ridgekeyctl does.
import PackageDescription

let package = Package(
    name: "RidgeKeyApp",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(name: "RidgeKey", path: "Sources/RidgeKey")
    ]
)
