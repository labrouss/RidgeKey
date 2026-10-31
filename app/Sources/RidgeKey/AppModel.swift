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
    @Published var sensorID = ""      // vvvv:pppp
    @Published var sensorDriver = ""
    @Published var consoleUser = ""
    @Published var refreshing = false

    // prints
    @Published var fingers: [String] = []

    // lock-screen password
    @Published var passwordStored = false
    @Published var passwordValid = false

    // sudo
    @Published var sudoEnabled = false

    // admin dialogs
    @Published var adminEnabled = false
    @Published var adminMessage = ""

    // enroll / delete need a confirmation (0.9): an enrolled finger or the login password
    enum Confirmation { case finger, password(String) }
    enum PendingAction: Identifiable {
        case enroll(String), delete(String)
        var id: String {
            switch self {
            case .enroll(let f): return "enroll-" + f
            case .delete(let f): return "delete-" + f
            }
        }
        var title: String {
            switch self {
            case .enroll(let f): return "Enroll \(RK.label(f).lowercased())"
            case .delete(let f): return "Delete \(RK.label(f).lowercased())"
            }
        }
    }
    @Published var pendingAction: PendingAction?
    @Published var confirming = false      // waiting for the confirming finger

    func perform(_ action: PendingAction, _ confirmation: Confirmation) {
        pendingAction = nil
        switch action {
        case .enroll(let f): startEnroll(f, confirmation)
        case .delete(let f): Task { await delete(f, confirmation) }
        }
    }

    private func authFields(_ c: Confirmation) -> [String: Any] {
        switch c {
        case .finger: return ["auth": "finger"]
        case .password(let pw): return ["auth": "password", "password": pw]
        }
    }

    private func explain(_ r: RKReply) -> String {
        switch r.error {
        case "bad-password": return "that is not your login password"
        case "no-match": return "the finger is not one of your enrolled fingers"
        case "untrusted-client": return "only the installed RidgeKey app may do this - reinstall RidgeKey"
        default: return r.message
        }
    }

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
        sensorID = status.string("device-id") ?? ""
        sensorDriver = status.string("driver") ?? ""
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
        adminEnabled = await Self.readAdminEnabled()
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

    func startEnroll(_ finger: String, _ confirmation: Confirmation) {
        enrollFinger = finger
        enrollStage = 0
        enrollStages = 8
        if case .finger = confirmation {
            confirming = true
            enrollMessage = "Confirm it is you: touch one of your enrolled fingers."
        } else {
            confirming = false
            enrollMessage = "Touch the sensor with your \(RK.label(finger).lowercased()) finger."
        }
        fingerOnSensor = false
        enrolling = true
        Task {
            let r = await RKClient.call(
                (["op": "enroll", "finger": finger] as [String: Any]).merging(authFields(confirmation)) { $1 },
                onStart: { [weak self] op in self?.enrollOperation = op },
                onEvent: { [weak self] event in self?.handleEnrollEvent(event) })
            enrollOperation = nil
            enrolling = false
            confirming = false
            if r.ok {
                fingerprintsMessage = "✓ Enrolled \(RK.label(finger).lowercased())."
            } else if r.error == "cancelled" {
                fingerprintsMessage = "Enrollment cancelled."
            } else {
                fingerprintsMessage = "Enrollment failed: \(explain(r))"
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
        case "confirmed":
            confirming = false
            enrollMessage = "Confirmed. Now touch the sensor with your \(RK.label(enrollFinger).lowercased()) finger."
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

    func delete(_ finger: String, _ confirmation: Confirmation) async {
        if case .finger = confirmation {
            fingerprintsMessage = "Touch one of your enrolled fingers to confirm…"
        }
        let r = await RKClient.call((["op": "delete", "finger": finger] as [String: Any]).merging(authFields(confirmation)) { $1 })
        fingerprintsMessage = r.ok ? "Deleted \(RK.label(finger).lowercased())." : "Delete failed: \(explain(r))"
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
        } else if r.error == "untrusted-client" {
            passwordMessage = "✗ Only the installed RidgeKey app may do this - reinstall RidgeKey."
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

    /// Runs a command as root through the standard administrator authorization dialog
    /// (which itself accepts a fingerprint once admin dialogs are enabled).
    private func runPrivileged(_ command: String) async -> (ok: Bool, output: String) {
        await Task.yield()   // let the status text appear before the modal dialog
        // NSAppleScript belongs on the main thread; the dialog is modal anyway.
        let script = "do shell script \"\(command) 2>&1\" with administrator privileges"
        var error: NSDictionary?
        let out = NSAppleScript(source: script)?.executeAndReturnError(&error)
        return error.map {
            (false, $0[NSAppleScript.errorMessage] as? String ?? "cancelled")
        } ?? (true, out?.stringValue ?? "")
    }

    func setSudo(_ enable: Bool) async {
        sudoMessage = enable ? "Enabling…" : "Disabling…"
        let result = await runPrivileged("\(RK.sudoTool) \(enable ? "enable" : "disable")")
        sudoMessage = result.ok ? (result.output.split(separator: "\n").first.map(String.init) ?? "") : "✗ \(result.output)"
        sudoEnabled = Self.readSudoEnabled()
    }

    // MARK: admin dialogs

    /// system.preferences is the first right ridgekey-admin switches; anyone may read the rules.
    nonisolated static func readAdminEnabled() async -> Bool {
        await Task.detached {
            let p = Process()
            let pipe = Pipe()
            p.executableURL = URL(fileURLWithPath: "/usr/bin/security")
            p.arguments = ["authorizationdb", "read", "system.preferences"]
            p.standardOutput = pipe
            p.standardError = FileHandle.nullDevice
            guard (try? p.run()) != nil else { return false }
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            p.waitUntilExit()
            return String(decoding: data, as: UTF8.self).contains(RK.fingerprintRule)
        }.value
    }

    func setAdmin(_ enable: Bool) async {
        adminMessage = enable ? "Enabling…" : "Disabling…"
        let result = await runPrivileged("\(RK.adminTool) \(enable ? "enable" : "disable")")
        let lines = result.output.split(separator: "\n").map(String.init)
        let switched = lines.filter { $0.hasPrefix("  ok ") }.count
        if !result.ok {
            adminMessage = "✗ " + (lines.last(where: { !$0.isEmpty }) ?? result.output)
        } else if enable {
            adminMessage = "Fingerprint enabled for \(switched) admin action\(switched == 1 ? "" : "s")."
        } else {
            adminMessage = "Normal password dialogs restored (\(switched))."
        }
        adminEnabled = await Self.readAdminEnabled()
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
