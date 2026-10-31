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

    // finger actions (0.10)
    struct FingerAction: Identifiable, Equatable {
        var id = UUID()
        var finger: String
        var hold: Bool
        var seconds: Int
        var taps: Int = 1            // tap only: 1 single, 2 double, 3 triple
        /// "1", "2", "3" = taps; "hold" = hold (one picker in the UI)
        var gesture: String {
            get { hold ? "hold" : String(taps) }
            set { if newValue == "hold" { hold = true } else { hold = false; taps = Int(newValue) ?? 1 } }
        }
        var action: String          // "lock" | "shortcut"
        var shortcut: String

        /// What triggers it: finger + gesture. Each trigger can have one action.
        var trigger: String { finger + "|" + (hold ? "hold-\(seconds)" : "tap-\(taps)") }

        /// Same rule = same content (ids are only for SwiftUI lists).
        static func == (a: FingerAction, b: FingerAction) -> Bool {
            a.finger == b.finger && a.hold == b.hold && (!a.hold || a.seconds == b.seconds)
                && (a.hold || a.taps == b.taps)
                && a.action == b.action && (a.action != "shortcut" || a.shortcut == b.shortcut)
        }
    }
    @Published var actions: [FingerAction] = []
    @Published var shortcutNames: [String] = []    // from the Shortcuts app (shortcuts list)
    @Published var listening = false
    @Published var actionsMessage = ""

    // admin dialogs
    @Published var adminEnabled = false
    @Published var adminMessage = ""

    // enroll / delete need a confirmation (0.9): an enrolled finger or the login password
    enum Confirmation { case finger, password(String) }
    enum PendingAction: Identifiable {
        case enroll(String), delete(String), saveActions([FingerAction])
        var id: String {
            switch self {
            case .enroll(let f): return "enroll-" + f
            case .delete(let f): return "delete-" + f
            case .saveActions: return "save-actions"
            }
        }
        var title: String {
            switch self {
            case .enroll(let f): return "Enroll \(RK.label(f).lowercased())"
            case .delete(let f): return "Delete \(RK.label(f).lowercased())"
            case .saveActions: return "Save finger actions"
            }
        }
    }
    /// Password-only confirmation sheet (no finger enrolled yet).
    @Published var pendingAction: PendingAction?
    /// The enroll / delete running in the activity sheet.
    @Published var activity: PendingAction?
    @Published var confirming = false      // waiting for the confirming finger (or a password)
    private var currentOperation: RKOperation?
    private var restartWith: Confirmation?

    /// Enroll / delete button: with an enrolled finger, start listening for it right away (the
    /// sheet also takes a password); without one, ask for the password first.
    func request(_ action: PendingAction) {
        if fingers.isEmpty {
            pendingAction = action
        } else {
            run(action, .finger)
        }
    }

    func perform(_ action: PendingAction, _ confirmation: Confirmation) {
        pendingAction = nil
        run(action, confirmation)
    }

    /// Typed a password while the finger confirmation was waiting: stop that and redo with it.
    func usePassword(_ password: String) {
        restartWith = .password(password)
        currentOperation?.cancel()
    }

    func cancelActivity() {
        restartWith = nil
        currentOperation?.cancel()
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
    @Published var enrollFinger = ""
    @Published var enrollStage = 0
    @Published var enrollStages = 8
    @Published var enrollMessage = ""
    @Published var fingerOnSensor = false

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
        listening = status.bool("listening")

        if reachable {
            let list = await RKClient.call(["op": "list"])
            fingers = list.strings("fingers")
            let pw = await RKClient.call(["op": "password-check"])
            passwordStored = pw.bool("password-stored")
            passwordValid = pw.bool("password-valid")
            let a = await RKClient.call(["op": "actions-get"])
            actions = ((a.fields["actions"] as? [Any]) ?? []).compactMap { item in
                guard let d = item as? [String: Any], let f = d["finger"] as? String else { return nil }
                return FingerAction(finger: f, hold: (d["gesture"] as? String) == "hold",
                                    seconds: max(d["seconds"] as? Int ?? 2, 1),
                                    taps: max(d["taps"] as? Int ?? 1, 1),
                                    action: d["action"] as? String ?? "lock",
                                    shortcut: d["shortcut"] as? String ?? "")
            }
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

    private func run(_ action: PendingAction, _ confirmation: Confirmation) {
        var request: [String: Any]
        var (op, finger) = ("", "")
        switch action {
        case .enroll(let f): (op, finger) = ("enroll", f); request = ["op": op, "finger": f]
        case .delete(let f): (op, finger) = ("delete", f); request = ["op": op, "finger": f]
        case .saveActions(let list): op = "actions-set"; request = ["op": op, "actions": Self.rulesPayload(list)]
        }
        let label = RK.label(finger).lowercased()
        enrollFinger = finger
        enrollStage = 0
        enrollStages = 8
        fingerOnSensor = false
        if case .finger = confirmation {
            confirming = true
            enrollMessage = "Touch one of your enrolled fingers - or type your login password."
        } else {
            confirming = false
            enrollMessage = op == "enroll" ? "Touch the sensor with your \(label) finger."
                : op == "delete" ? "Deleting…" : "Saving…"
        }
        activity = action
        Task {
            let r = await RKClient.call(
                request.merging(authFields(confirmation)) { $1 },
                onStart: { [weak self] o in self?.currentOperation = o },
                onEvent: { [weak self] event in self?.handleEvent(event) })
            currentOperation = nil
            if r.error == "cancelled", let next = restartWith {
                restartWith = nil
                run(action, next)          // same sheet, now with the typed password
                return
            }
            activity = nil
            confirming = false
            if case .saveActions(let list) = action {
                if r.ok {
                    // ridgekeyd restarts listening ~0.5 s after a change; read the state after that
                    try? await Task.sleep(nanoseconds: 1_500_000_000)
                    actionsMessage = list.isEmpty ? "Finger actions off."
                        : "✓ Saved \(list.count) finger action\(list.count == 1 ? "" : "s")."
                } else {
                    actionsMessage = r.error == "cancelled" ? "Not saved." : "✗ " + explain(r)
                }
            } else if r.ok {
                fingerprintsMessage = op == "enroll" ? "✓ Enrolled \(label)." : "✓ Deleted \(label)."
            } else if r.error == "cancelled" {
                fingerprintsMessage = "Cancelled."
            } else {
                fingerprintsMessage = (op == "enroll" ? "Enrollment failed: " : "Delete failed: ") + explain(r)
            }
            await refresh()
        }
    }

    private func handleEvent(_ event: [String: Any]) {
        switch event["event"] as? String {
        case "enroll-progress":
            enrollStage = event["stage"] as? Int ?? enrollStage
            enrollStages = max(event["stages"] as? Int ?? enrollStages, 1)
            enrollMessage = enrollStage < enrollStages ? "Good. Lift your finger and touch again." : "Done."
        case "confirmed":
            confirming = false
            if case .delete = activity {
                enrollMessage = "Confirmed. Deleting…"
            } else if case .saveActions = activity {
                enrollMessage = "Confirmed. Saving…"
            } else {
                enrollMessage = "Confirmed. Now touch the sensor with your \(RK.label(enrollFinger).lowercased()) finger."
            }
        case "retry":
            enrollMessage = (event["message"] as? String ?? "Try again") + " - touch again."
        case "finger":
            fingerOnSensor = event["present"] as? Bool ?? false
        default:
            break
        }
    }

    // MARK: finger actions

    /// The user's Shortcuts, as the Shortcuts app lists them (`shortcuts list`).
    func loadShortcuts() async {
        let names: [String] = await Task.detached {
            let p = Process()
            let pipe = Pipe()
            p.executableURL = URL(fileURLWithPath: "/usr/bin/shortcuts")
            p.arguments = ["list"]
            p.standardOutput = pipe
            p.standardError = FileHandle.nullDevice
            guard (try? p.run()) != nil else { return [] }
            let data = pipe.fileHandleForReading.readDataToEndOfFile()
            p.waitUntilExit()
            return String(decoding: data, as: UTF8.self)
                .split(separator: "\n").map { $0.trimmingCharacters(in: .whitespaces) }
                .filter { !$0.isEmpty }
                .sorted { $0.localizedCaseInsensitiveCompare($1) == .orderedAscending }
        }.value
        shortcutNames = names
    }

    /// Cheap status poll for the Finger Actions pane (listening starts/stops on its own).
    func refreshListening() async {
        let s = await RKClient.call(["op": "status"])
        listening = s.reachable && s.bool("listening")
    }

    /// Saving finger actions is confirmed like enroll (an enrolled finger or the password).
    func saveActions(_ list: [FingerAction]) {
        actionsMessage = ""
        request(.saveActions(list))
    }

    static func rulesPayload(_ list: [FingerAction]) -> [Any] {
        list.map { a in
            ["finger": a.finger, "gesture": a.hold ? "hold" : "tap", "seconds": a.hold ? a.seconds : 0,
             "taps": a.hold ? 1 : a.taps,
             "action": a.action, "shortcut": a.action == "shortcut" ? a.shortcut : ""] as [String: Any]
        }
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
