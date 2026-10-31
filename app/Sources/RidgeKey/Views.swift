// RidgeKey - SwiftUI views.
// SPDX-License-Identifier: MIT
import AppKit
import SwiftUI

enum Pane: String, CaseIterable, Identifiable {
    case overview, fingerprints, actions, lockScreen, admin, sudo, about
    var id: String { rawValue }

    var title: String {
        switch self {
        case .overview: return "Overview"
        case .fingerprints: return "Fingerprints"
        case .actions: return "Finger Actions"
        case .lockScreen: return "Lock Screen"
        case .admin: return "Admin Dialogs"
        case .sudo: return "sudo"
        case .about: return "About"
        }
    }

    var icon: String {
        switch self {
        case .overview: return "gauge"
        case .fingerprints: return "touchid"
        case .actions: return "hand.tap"
        case .lockScreen: return "lock.display"
        case .admin: return "lock.shield"
        case .sudo: return "terminal"
        case .about: return "info.circle"
        }
    }
}

struct ContentView: View {
    @EnvironmentObject var model: AppModel
    @State private var selection: Pane? = .overview

    var body: some View {
        NavigationSplitView {
            List(Pane.allCases, selection: $selection) { pane in
                Label(pane.title, systemImage: pane.icon).tag(pane)
            }
            .navigationSplitViewColumnWidth(min: 170, ideal: 190)
        } detail: {
            Group {
                switch selection ?? .overview {
                case .overview: OverviewView()
                case .fingerprints: FingerprintsView()
                case .lockScreen: LockScreenView()
                case .actions: ActionsView()
                case .admin: AdminView()
                case .sudo: SudoView()
                case .about: AboutView()
                }
            }
            .formStyle(.grouped)
            .navigationTitle((selection ?? .overview).title)
        }
        .toolbar {
            ToolbarItem {
                Button {
                    Task { await model.refresh() }
                } label: {
                    Label("Refresh", systemImage: "arrow.clockwise")
                }
                .disabled(model.refreshing)
            }
        }
        .task { await model.refresh() }
        .sheet(item: $model.activity) { ActivitySheet(action: $0) }
    }
}

/// A status row: green check, orange warning or grey dash.
struct StatusRow: View {
    enum Kind { case good, warn, off }
    let title: String
    let detail: String
    let state: Kind

    var body: some View {
        LabeledContent {
            HStack(spacing: 6) {
                Text(detail).foregroundStyle(.secondary)
                Image(systemName: icon).foregroundStyle(color)
            }
        } label: {
            Text(title)
        }
    }

    private var icon: String {
        switch state {
        case .good: return "checkmark.circle.fill"
        case .warn: return "exclamationmark.triangle.fill"
        case .off: return "minus.circle"
        }
    }

    private var color: Color {
        switch state {
        case .good: return .green
        case .warn: return .orange
        case .off: return .secondary
        }
    }
}

// MARK: - Overview

struct OverviewView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        Form {
            Section("Service") {
                StatusRow(title: "ridgekeyd",
                          detail: model.reachable ? "running \(model.version)" : "not running",
                          state: model.reachable ? .good : .warn)
                StatusRow(title: "Sensor",
                          detail: model.sensor.isEmpty ? "no supported reader found" : model.sensor,
                          state: model.sensor.isEmpty ? .warn : .good)
                if !model.sensor.isEmpty {
                    LabeledContent("Reader", value: "\(model.sensorDriver) driver"
                                   + (model.sensorID.isEmpty ? "" : " · \(model.sensorID)"))
                    if model.sensorDriver != "synaptics" {
                        Text("This reader's driver has not been tested with RidgeKey yet. If it works "
                             + "(or not), please report it on GitHub with the reader ID above.")
                            .font(.footnote).foregroundStyle(.secondary)
                    }
                }
                LabeledContent("Logged-in user", value: model.consoleUser.isEmpty ? "-" : model.consoleUser)
            }
            Section("Set up") {
                StatusRow(title: "Fingerprints",
                          detail: model.fingers.isEmpty ? "none enrolled" : "\(model.fingers.count) enrolled",
                          state: model.fingers.isEmpty ? .warn : .good)
                StatusRow(title: "Lock-screen password",
                          detail: !model.passwordStored ? "not stored" : (model.passwordValid ? "stored" : "outdated"),
                          state: !model.passwordStored ? .off : (model.passwordValid ? .good : .warn))
                StatusRow(title: "Fingerprint for sudo",
                          detail: model.sudoEnabled ? "on" : "off",
                          state: model.sudoEnabled ? .good : .off)
            }
            Section("Try it") {
                HStack {
                    Button("Test my fingerprint") { Task { await model.verify() } }
                        .disabled(!model.reachable || model.fingers.isEmpty)
                    Text(model.verifyResult).foregroundStyle(.secondary)
                }
            }
        }
    }
}

// MARK: - Fingerprints

struct FingerprintsView: View {
    @EnvironmentObject var model: AppModel
    @State private var finger = RK.fingers[0]

    var body: some View {
        Form {
            Section("Enrolled") {
                if model.fingers.isEmpty {
                    Text("No fingerprints enrolled yet.").foregroundStyle(.secondary)
                }
                ForEach(model.fingers, id: \.self) { f in
                    HStack {
                        Label(RK.label(f), systemImage: "touchid")
                        Spacer()
                        Button(role: .destructive) {
                            model.request(.delete(f))
                        } label: {
                            Image(systemName: "trash")
                        }
                        .buttonStyle(.borderless)
                        .help("Delete this fingerprint from the sensor")
                    }
                }
            }
            Section("Add a fingerprint") {
                Picker("Finger", selection: $finger) {
                    ForEach(RK.fingers, id: \.self) { f in
                        Text(RK.label(f) + (model.fingers.contains(f) ? " (replace)" : "")).tag(f)
                    }
                }
                Button("Enroll…") { model.request(.enroll(finger)) }
                    .disabled(!model.reachable || model.sensor.isEmpty)
                if !model.fingerprintsMessage.isEmpty {
                    Text(model.fingerprintsMessage).foregroundStyle(.secondary)
                }
            }
            Section {
                Text("RidgeKey only deletes fingerprints it enrolled itself. Windows Hello and BIOS "
                     + "fingerprints stored on the same sensor are never touched.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .sheet(item: $model.pendingAction) { ConfirmSheet(action: $0) }
    }
}

/// No finger enrolled yet: the login password confirms enroll / delete.
struct ConfirmSheet: View {
    @EnvironmentObject var model: AppModel
    let action: AppModel.PendingAction
    @State private var password = ""

    var body: some View {
        VStack(spacing: 14) {
            Image(systemName: "lock.shield").font(.system(size: 44)).foregroundStyle(Color.accentColor)
            Text(action.title).font(.headline)
            Text("Confirm with your login password.").foregroundStyle(.secondary)
            SecureField("Login password", text: $password)
                .onSubmit { if !password.isEmpty { model.perform(action, .password(password)) } }
            HStack {
                Button("Cancel", role: .cancel) { model.pendingAction = nil }
                    .keyboardShortcut(.cancelAction)
                Button("Continue") { model.perform(action, .password(password)) }
                    .keyboardShortcut(.defaultAction)
                    .disabled(password.isEmpty)
            }
        }
        .padding(24)
        .frame(width: 340)
    }
}

/// Enroll / delete in progress. While confirming it listens for an enrolled finger AND takes a
/// password - whichever comes first.
struct ActivitySheet: View {
    @EnvironmentObject var model: AppModel
    let action: AppModel.PendingAction
    @State private var password = ""

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: model.confirming ? "lock.shield" : "touchid")
                .font(.system(size: 56))
                .foregroundStyle(model.fingerOnSensor ? Color.accentColor : Color.secondary)
            Text(model.confirming ? "\(action.title) - confirm it is you" : action.title).font(.headline)
            if case .enroll = action, !model.confirming {
                ProgressView(value: Double(model.enrollStage), total: Double(max(model.enrollStages, 1)))
                Text("\(model.enrollStage) of \(model.enrollStages)").font(.caption).foregroundStyle(.secondary)
            }
            Text(model.enrollMessage).multilineTextAlignment(.center)
            if model.confirming {
                SecureField("Login password", text: $password)
                    .onSubmit(submit)
            }
            HStack {
                Button("Cancel", role: .cancel) { model.cancelActivity() }
                    .keyboardShortcut(.cancelAction)
                if model.confirming {
                    Button("Use password", action: submit)
                        .keyboardShortcut(.defaultAction)
                        .disabled(password.isEmpty)
                }
            }
        }
        .padding(24)
        .frame(width: 380)
        .interactiveDismissDisabled()
    }

    private func submit() {
        let pw = password
        password = ""
        if !pw.isEmpty { model.usePassword(pw) }
    }
}

// MARK: - Lock Screen

struct LockScreenView: View {
    @EnvironmentObject var model: AppModel
    @State private var password = ""

    var body: some View {
        Form {
            Section("Stored login password") {
                StatusRow(title: "Status",
                          detail: !model.passwordStored ? "not stored"
                              : (model.passwordValid ? "stored and valid" : "outdated - store it again"),
                          state: !model.passwordStored ? .off : (model.passwordValid ? .good : .warn))
                SecureField("Login password", text: $password)
                HStack {
                    Button("Check and store") {
                        let pw = password
                        password = ""
                        Task { await model.setPassword(pw) }
                    }
                    .disabled(password.isEmpty || !model.reachable)
                    Button("Remove", role: .destructive) { Task { await model.clearPassword() } }
                        .disabled(!model.passwordStored)
                }
                if !model.passwordMessage.isEmpty {
                    Text(model.passwordMessage).foregroundStyle(.secondary)
                }
            }
            Section("Accessibility permission") {
                Text("RidgeKey Agent types your password into the lock screen after a fingerprint match. "
                     + "It needs Accessibility permission (only once - it keeps its signature across upgrades).")
                HStack {
                    Button("Open Accessibility settings") { model.openAccessibilitySettings() }
                    Button("Show RidgeKey Agent in Finder") { model.revealAgent() }
                }
            }
            Section("How it works") {
                Text("Your password is checked against your account and kept in the System keychain, "
                     + "readable only by root. It is handed only to RidgeKey Agent, only right after a "
                     + "fingerprint match, and only while your screen is locked. Lock with Ctrl-Cmd-Q, "
                     + "then touch the sensor.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }
}

// MARK: - sudo

struct SudoView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        Form {
            Section {
                Toggle("Use fingerprint for sudo",
                       isOn: Binding(get: { model.sudoEnabled },
                                     set: { on in Task { await model.setSudo(on) } }))
                if !model.sudoMessage.isEmpty {
                    Text(model.sudoMessage).foregroundStyle(.secondary)
                }
            }
            Section("How it works") {
                Text("Adds pam_ridgekey to /etc/pam.d/sudo_local. sudo then asks for your fingerprint "
                     + "first; a wrong finger, a timeout, Ctrl-C or SSH sessions fall back to your password. "
                     + "Changing this asks for an administrator password.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }
}

// MARK: - finger actions

struct ActionsView: View {
    @EnvironmentObject var model: AppModel
    @State private var draft: [AppModel.FingerAction] = []
    @State private var loaded = false

    var body: some View {
        Form {
            Section {
                if model.fingers.isEmpty {
                    Text("Enroll a finger first (Fingerprints).").foregroundStyle(.secondary)
                }
                ForEach($draft) { $a in
                    VStack(alignment: .leading, spacing: 6) {
                        HStack {
                            Picker("", selection: $a.finger) {
                                ForEach(model.fingers, id: \.self) { Text(RK.label($0)).tag($0) }
                            }
                            .labelsHidden().frame(width: 150)
                            Picker("", selection: $a.gesture) {
                                Text("single tap").tag("1")
                                Text("double tap").tag("2")
                                Text("triple tap").tag("3")
                                Text("hold").tag("hold")
                            }
                            .labelsHidden().frame(width: 120)
                            if a.hold {
                                Stepper("\(a.seconds) s", value: $a.seconds, in: 1...10).frame(width: 90)
                            }
                            Spacer()
                            Button(role: .destructive) { draft.removeAll { $0.id == a.id } } label: {
                                Image(systemName: "trash")
                            }
                            .buttonStyle(.borderless)
                        }
                        HStack {
                            Text("→")
                            Picker("", selection: $a.action) {
                                Text("Lock the screen").tag("lock")
                                Text("Run a Shortcut").tag("shortcut")
                            }
                            .labelsHidden().frame(width: 170)
                            if a.action == "shortcut" {
                                TextField("Shortcut name", text: $a.shortcut)
                            }
                        }
                    }
                    .padding(.vertical, 4)
                }
                HStack {
                    Button("Add action") {
                        let f = model.fingers.first ?? "right-index"
                        draft.append(.init(finger: f, hold: false, seconds: 2, action: "lock", shortcut: ""))
                    }
                    .disabled(model.fingers.isEmpty || draft.count >= 20)
                    Spacer()
                    Button("Save") { Task { await model.saveActions(draft) } }
                        .disabled(!model.reachable || draft == model.actions ||
                                  draft.contains { $0.action == "shortcut" && $0.shortcut.isEmpty })
                        .keyboardShortcut(.defaultAction)
                }
                if !model.actionsMessage.isEmpty {
                    Text(model.actionsMessage).foregroundStyle(.secondary)
                }
                StatusRow(title: "Listening", detail: model.listening ? "waiting for your fingers" : "off",
                          state: model.listening ? .good : .off)
            }
            Section("How it works") {
                Text("While your screen is unlocked and the sensor is idle, RidgeKey waits for one of "
                     + "your enrolled fingers: a single, double or triple tap, or a hold of N seconds "
                     + "(released). Double/triple taps wait ~0.7 s for the next tap, only on fingers that "
                     + "have such a rule. The sensor waits "
                     + "in hardware, so nothing polls. sudo, the lock screen and admin dialogs always come "
                     + "first. Shortcuts are the ones you build in the Shortcuts app; RidgeKey Agent runs them as you.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
        .onAppear { if !loaded { draft = model.actions; loaded = true } }
        .onChange(of: model.actions) { new in if draft.isEmpty || !loaded { draft = new } }
    }
}

// MARK: - admin dialogs

struct AdminView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        Form {
            Section {
                Toggle("Use fingerprint for admin dialogs",
                       isOn: Binding(get: { model.adminEnabled },
                                     set: { on in Task { await model.setAdmin(on) } }))
                if !model.adminMessage.isEmpty {
                    Text(model.adminMessage).foregroundStyle(.secondary)
                }
                StatusRow(title: "Stored password",
                          detail: model.passwordStored ? (model.passwordValid ? "stored and valid" : "outdated")
                                                       : "not stored - set it under Lock Screen",
                          state: model.passwordStored && model.passwordValid ? .good : .warn)
            }
            Section("How it works") {
                Text("System Settings locks, installers and other \"wants to make changes\" requests show "
                     + "a RidgeKey notice first: touch the sensor with an administrator's finger. RidgeKey "
                     + "hands that administrator's stored password to macOS, which checks it as usual. "
                     + "A wrong finger, a timeout or \"Use Password\" brings up the normal password dialog. "
                     + "Turning this off restores the original macOS rules exactly.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }
}

// MARK: - About

struct AboutView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        Form {
            if let logo = Self.logo {
                Section {
                    Image(nsImage: logo)
                        .resizable()
                        .aspectRatio(contentMode: .fit)
                        .frame(maxWidth: .infinity)
                        .clipShape(RoundedRectangle(cornerRadius: 10))
                        .accessibilityLabel("RidgeKey")
                }
                .listRowInsets(EdgeInsets())
            }
            Section {
                LabeledContent("RidgeKey app", value: appVersion)
                LabeledContent("ridgekeyd", value: model.reachable ? model.version : "not running")
                Link("github.com/labrouss/RidgeKey", destination: URL(string: "https://github.com/labrouss/RidgeKey")!)
            }
            Section {
                Text("Fingerprint unlock for macOS on laptops with match-on-chip readers (Synaptics, "
                     + "Goodix, Elan, FPC, Egis, FocalTech, Realtek), "
                     + "built on libfprint. MIT licensed; libfprint, GLib, gusb and libusb are LGPL. "
                     + "Not affiliated with Apple, Synaptics, HP or the libfprint project.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }

    private static let logo: NSImage? = Bundle.main.url(forResource: "RidgeKeyLogo", withExtension: "png")
        .flatMap { NSImage(contentsOf: $0) }

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }
}
