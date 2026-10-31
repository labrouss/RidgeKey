// RidgeKey - SwiftUI views.
// SPDX-License-Identifier: MIT
import SwiftUI

enum Pane: String, CaseIterable, Identifiable {
    case overview, fingerprints, lockScreen, sudo, about
    var id: String { rawValue }

    var title: String {
        switch self {
        case .overview: return "Overview"
        case .fingerprints: return "Fingerprints"
        case .lockScreen: return "Lock Screen"
        case .sudo: return "sudo"
        case .about: return "About"
        }
    }

    var icon: String {
        switch self {
        case .overview: return "gauge"
        case .fingerprints: return "touchid"
        case .lockScreen: return "lock.display"
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
        .sheet(isPresented: $model.enrolling) { EnrollSheet() }
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
                          detail: model.sensor.isEmpty ? "none found" : model.sensor,
                          state: model.sensor.isEmpty ? .warn : .good)
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
    @State private var pendingDelete: String?

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
                            pendingDelete = f
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
                Button("Enroll…") { model.startEnroll(finger) }
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
        .confirmationDialog("Delete the \(RK.label(pendingDelete ?? "").lowercased()) fingerprint?",
                            isPresented: Binding(get: { pendingDelete != nil },
                                                 set: { if !$0 { pendingDelete = nil } })) {
            Button("Delete", role: .destructive) {
                if let f = pendingDelete { Task { await model.delete(f) } }
                pendingDelete = nil
            }
        }
    }
}

struct EnrollSheet: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "touchid")
                .font(.system(size: 56))
                .foregroundStyle(model.fingerOnSensor ? Color.accentColor : Color.secondary)
            Text("Enrolling \(RK.label(model.enrollFinger).lowercased())").font(.headline)
            ProgressView(value: Double(model.enrollStage), total: Double(max(model.enrollStages, 1)))
            Text("\(model.enrollStage) of \(model.enrollStages)").font(.caption).foregroundStyle(.secondary)
            Text(model.enrollMessage).multilineTextAlignment(.center)
            Button("Cancel", role: .cancel) { model.cancelEnroll() }
                .keyboardShortcut(.cancelAction)
        }
        .padding(24)
        .frame(width: 380)
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
                Text("ridgekey-agent types your password into the lock screen after a fingerprint match. "
                     + "It needs Accessibility permission (only once - it keeps its signature across upgrades).")
                HStack {
                    Button("Open Accessibility settings") { model.openAccessibilitySettings() }
                    Button("Show ridgekey-agent in Finder") { model.revealAgent() }
                }
            }
            Section("How it works") {
                Text("Your password is checked against your account and kept in the System keychain, "
                     + "readable only by root. It is handed only to ridgekey-agent, only right after a "
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

// MARK: - About

struct AboutView: View {
    @EnvironmentObject var model: AppModel

    var body: some View {
        Form {
            Section {
                LabeledContent("RidgeKey app", value: appVersion)
                LabeledContent("ridgekeyd", value: model.reachable ? model.version : "not running")
                Link("github.com/labrouss/RidgeKey", destination: URL(string: "https://github.com/labrouss/RidgeKey")!)
            }
            Section {
                Text("Fingerprint unlock for macOS on laptops with Synaptics match-in-sensor readers, "
                     + "built on libfprint. MIT licensed; libfprint, GLib, gusb and libusb are LGPL. "
                     + "Not affiliated with Apple, Synaptics, HP or the libfprint project.")
                    .font(.footnote).foregroundStyle(.secondary)
            }
        }
    }

    private var appVersion: String {
        Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"
    }
}
