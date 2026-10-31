// RidgeKey - app state and actions.
// SPDX-License-Identifier: MIT
import AppKit
import Foundation

@MainActor
final class AppModel: ObservableObject {
    // service
    @Published var reachable = false
    @Published var version = ""
    @Published var sensor = ""
    @Published var consoleUser = ""
    @Published var refreshing = false

    // prints
    @Published var fingers: [String] = []

    // lock-screen password
    @Published var passwordStored = false
    @Published var passwordValid = false

    // sudo
    @Published var sudoEnabled = false

    // enrollment in progress
    @Published var enrolling = false
    @Published var enrollFinger = ""
    @Published var enrollStage = 0
    @Published var enrollStages = 8
    @Published var enrollMessage = ""
    @Published var fingerOnSensor = false
    private var enrollOperation: RKOperation?

    // one-line results shown in the views
    @Published var verifyResult = ""
    @Published var fingerprintsMessage = ""
    @Published var passwordMessage = ""
    @Published var sudoMessage = ""

    // MARK: refresh

    func refresh() async {
        refreshing = true
        defer { refreshing = false }

        let status = await RKClient.call(["op": "status"])
        reachable = status.reachable && status.ok
        version = status.string("version") ?? ""
        sensor = status.string("device") ?? ""
        consoleUser = status.string("console-user") ?? ""

        if reachable {
            let list = await RKClient.call(["op": "list"])
            fingers = list.strings("fingers")
            let pw = await RKClient.call(["op": "password-check"])
            passwordStored = pw.bool("password-stored")
            passwordValid = pw.bool("password-valid")
        } else {
            fingers = []
            passwordStored = false
            passwordValid = false
        }
        sudoEnabled = Self.readSudoEnabled()
    }

    // MARK: verify

    func verify() async {
        verifyResult = "Touch the sensor…"
        let r = await RKClient.call(["op": "verify", "timeout": 15])
        if r.ok {
            verifyResult = r.bool("matched")
                ? "✓ Recognised: \(RK.label(r.string("finger") ?? ""))"
                : "✗ Not recognised"
        } else {
            verifyResult = r.message
        }
    }

    // MARK: enroll / delete

    func startEnroll(_ finger: String) {
        enrollFinger = finger
        enrollStage = 0
        enrollStages = 8
        enrollMessage = "Touch the sensor with your \(RK.label(finger).lowercased()) finger."
        fingerOnSensor = false
        enrolling = true
        Task {
            let r = await RKClient.call(
                ["op": "enroll", "finger": finger],
                onStart: { [weak self] op in self?.enrollOperation = op },
                onEvent: { [weak self] event in self?.handleEnrollEvent(event) })
            enrollOperation = nil
            enrolling = false
            if r.ok {
                fingerprintsMessage = "✓ Enrolled \(RK.label(finger).lowercased())."
            } else if r.error == "cancelled" {
                fingerprintsMessage = "Enrollment cancelled."
            } else {
                fingerprintsMessage = "Enrollment failed: \(r.message)"
            }
            await refresh()
        }
    }

    private func handleEnrollEvent(_ event: [String: Any]) {
        switch event["event"] as? String {
        case "enroll-progress":
            enrollStage = event["stage"] as? Int ?? enrollStage
            enrollStages = max(event["stages"] as? Int ?? enrollStages, 1)
            enrollMessage = enrollStage < enrollStages ? "Good. Lift your finger and touch again." : "Done."
        case "retry":
            enrollMessage = (event["message"] as? String ?? "Try again") + " - touch again."
        case "finger":
            fingerOnSensor = event["present"] as? Bool ?? false
        default:
            break
        }
    }

    func cancelEnroll() {
        enrollOperation?.cancel()
    }

    func delete(_ finger: String) async {
        let r = await RKClient.call(["op": "delete", "finger": finger])
        fingerprintsMessage = r.ok ? "Deleted \(RK.label(finger).lowercased())." : "Delete failed: \(r.message)"
        await refresh()
    }

    // MARK: lock-screen password

    func setPassword(_ password: String) async {
        passwordMessage = "Checking…"
        let r = await RKClient.call(["op": "password-set", "password": password])
        if r.ok {
            passwordMessage = "✓ Password checked and stored."
        } else if r.error == "bad-password" {
            passwordMessage = "✗ That is not your login password."
        } else {
            passwordMessage = "✗ \(r.message)"
        }
        await refresh()
    }

    func clearPassword() async {
        let r = await RKClient.call(["op": "password-clear"])
        passwordMessage = r.ok ? "Stored password removed." : "✗ \(r.message)"
        await refresh()
    }

    // MARK: sudo

    nonisolated static func readSudoEnabled() -> Bool {
        guard let text = try? String(contentsOfFile: "/etc/pam.d/sudo_local", encoding: .utf8) else { return false }
        return text.split(separator: "\n").contains { line in
            let t = line.trimmingCharacters(in: .whitespaces)
            return !t.hasPrefix("#") && t.contains("pam_ridgekey.so")
        }
    }

    /// Runs ridgekey-sudo through the standard administrator authorization dialog.
    func setSudo(_ enable: Bool) async {
        let command = "\(RK.sudoTool) \(enable ? "enable" : "disable")"
        sudoMessage = enable ? "Enabling…" : "Disabling…"
        await Task.yield()   // let the "Enabling…" text appear before the modal dialog
        // NSAppleScript belongs on the main thread; the dialog is modal anyway.
        let script = "do shell script \"\(command)\" with administrator privileges"
        var error: NSDictionary?
        let out = NSAppleScript(source: script)?.executeAndReturnError(&error)
        let result: (ok: Bool, output: String) = error.map {
            (false, $0[NSAppleScript.errorMessage] as? String ?? "cancelled")
        } ?? (true, out?.stringValue ?? "")
        sudoMessage = result.ok ? (result.output.split(separator: "\n").first.map(String.init) ?? "") : "✗ \(result.output)"
        sudoEnabled = Self.readSudoEnabled()
    }

    // MARK: helpers

    func openAccessibilitySettings() {
        if let url = URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_Accessibility") {
            NSWorkspace.shared.open(url)
        }
    }

    func revealAgent() {
        NSWorkspace.shared.selectFile(RK.agentPath, inFileViewerRootedAtPath: "")
    }
}
