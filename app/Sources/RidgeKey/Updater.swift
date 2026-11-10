// RidgeKey - update check against GitHub Releases.
// SPDX-License-Identifier: MIT
//
// Asks api.github.com for the latest release (drafts and pre-releases are never "latest"),
// compares its version with this app's, downloads RidgeKey-<ver>.pkg into Downloads, verifies
// it against the release's .sha256 file and opens it in Installer. Nothing installs by itself:
// Installer asks for an administrator's password as for any package.
import AppKit
import CryptoKit
import Foundation

struct RKRelease: Equatable {
    let version: String         // "1.1.0"
    let page: URL               // release page with the notes
    let pkgName: String
    let pkgURL: URL
    let pkgSize: Int
    let shaURL: URL?
}

enum RKVersion {
    /// "v1.10.2-beta" -> [1, 10, 2]
    static func components(_ s: String) -> [Int] {
        var v = s.hasPrefix("v") ? String(s.dropFirst()) : s
        if let dash = v.firstIndex(of: "-") { v = String(v[..<dash]) }
        return v.split(separator: ".").map { Int($0) ?? 0 }
    }

    static func isNewer(_ a: String, than b: String) -> Bool {
        let x = components(a), y = components(b)
        for i in 0..<max(x.count, y.count) {
            let p = i < x.count ? x[i] : 0
            let q = i < y.count ? y[i] : 0
            if p != q { return p > q }
        }
        return false
    }
}

struct UpdateError: LocalizedError {
    let errorDescription: String?
    init(_ message: String) { errorDescription = message }
}

@MainActor
final class Updater: ObservableObject {
    enum Phase: Equatable {
        case idle, checking, upToDate, available, downloading
        case ready(URL)
        case failed(String)
    }

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var latest: RKRelease?
    @Published private(set) var lastChecked: Date?
    @Published var autoCheck: Bool {
        didSet { UserDefaults.standard.set(autoCheck, forKey: Self.autoKey) }
    }
    /// Set by "Check for Updates…" in the app menu; ContentView then shows About.
    @Published var showAbout = false

    static let api = URL(string: "https://api.github.com/repos/labrouss/RidgeKey/releases/latest")!
    static let releases = URL(string: "https://github.com/labrouss/RidgeKey/releases")!
    private static let autoKey = "updates.autoCheck"
    private static let lastKey = "updates.lastChecked"
    private static let launchInterval: TimeInterval = 6 * 3600

    /// This app's version. For testing the update flow, pretend to be older:
    ///   defaults write io.github.labrouss.ridgekey.app updates.pretendVersion 0.9.0
    let installed: String =
        UserDefaults.standard.string(forKey: "updates.pretendVersion")
        ?? Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "dev"

    init() {
        let d = UserDefaults.standard
        autoCheck = d.object(forKey: Self.autoKey) as? Bool ?? true
        lastChecked = d.object(forKey: Self.lastKey) as? Date
    }

    var updateAvailable: Bool {
        guard let l = latest, installed != "dev" else { return false }
        return RKVersion.isNewer(l.version, than: installed)
    }

    var busy: Bool { phase == .checking || phase == .downloading }

    /// Called when the window opens: at most once every 6 hours, and only if switched on.
    func checkOnLaunch() async {
        guard autoCheck, installed != "dev" else { return }
        if let last = lastChecked, Date().timeIntervalSince(last) < Self.launchInterval { return }
        await check()
    }

    func check() async {
        guard !busy else { return }
        phase = .checking
        do {
            var req = URLRequest(url: Self.api, timeoutInterval: 20)
            req.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
            req.setValue("RidgeKey/\(installed)", forHTTPHeaderField: "User-Agent")
            let (data, resp) = try await URLSession.shared.data(for: req)
            let code = (resp as? HTTPURLResponse)?.statusCode ?? 0
            switch code {
            case 200:
                latest = try Self.parse(data)
            case 404:
                latest = nil                                      // no release published yet
            case 403, 429:
                throw UpdateError("GitHub is rate-limiting requests from this network. Try again later.")
            default:
                throw UpdateError("GitHub answered HTTP \(code).")
            }
            lastChecked = Date()
            UserDefaults.standard.set(lastChecked, forKey: Self.lastKey)
            phase = updateAvailable ? .available : .upToDate
        } catch {
            phase = .failed(Self.describe(error))
        }
    }

    /// Downloads the package, verifies its SHA-256 and puts it in Downloads.
    func download() async {
        guard let rel = latest, !busy else { return }
        guard let shaURL = rel.shaURL else {
            phase = .failed("This release has no checksum file. Download it from the release page instead.")
            return
        }
        phase = .downloading
        let fm = FileManager.default
        var staged: URL?
        do {
            let (tmp, resp) = try await URLSession.shared.download(from: rel.pkgURL)
            guard (resp as? HTTPURLResponse)?.statusCode == 200 else {
                throw UpdateError("The download failed (HTTP \((resp as? HTTPURLResponse)?.statusCode ?? 0)).")
            }
            let stage = fm.temporaryDirectory.appendingPathComponent(UUID().uuidString + ".pkg")
            try fm.moveItem(at: tmp, to: stage)
            staged = stage

            let (shaData, shaResp) = try await URLSession.shared.data(from: shaURL)
            guard (shaResp as? HTTPURLResponse)?.statusCode == 200 else {
                throw UpdateError("Could not download the checksum file.")
            }
            let expected = String(decoding: shaData, as: UTF8.self)
                .split(whereSeparator: \.isWhitespace).first.map { $0.lowercased() } ?? ""
            let actual = try Self.sha256(of: stage)
            guard expected.count == 64, expected == actual else {
                throw UpdateError("The download does not match the release's checksum and was deleted.")
            }

            let dir = fm.urls(for: .downloadsDirectory, in: .userDomainMask).first ?? fm.temporaryDirectory
            let dest = dir.appendingPathComponent(rel.pkgName)
            if fm.fileExists(atPath: dest.path) { try fm.removeItem(at: dest) }
            try fm.moveItem(at: stage, to: dest)
            staged = nil
            phase = .ready(dest)
        } catch {
            if let s = staged { try? fm.removeItem(at: s) }
            phase = .failed(Self.describe(error))
        }
    }

    /// Opens the verified package in Installer.
    func install(_ pkg: URL) {
        NSWorkspace.shared.open(pkg)
    }

    // MARK: - helpers

    private struct APIRelease: Decodable {
        struct Asset: Decodable {
            let name: String
            let browser_download_url: URL
            let size: Int
        }
        let tag_name: String
        let html_url: URL
        let assets: [Asset]
    }

    static func parse(_ data: Data) throws -> RKRelease {
        let r = try JSONDecoder().decode(APIRelease.self, from: data)
        guard let pkg = r.assets.first(where: { $0.name.hasPrefix("RidgeKey-") && $0.name.hasSuffix(".pkg") })
        else { throw UpdateError("The latest release (\(r.tag_name)) has no installer package.") }
        let sha = r.assets.first { $0.name == pkg.name + ".sha256" }
        let version = r.tag_name.hasPrefix("v") ? String(r.tag_name.dropFirst()) : r.tag_name
        return RKRelease(version: version, page: r.html_url, pkgName: pkg.name,
                         pkgURL: pkg.browser_download_url, pkgSize: pkg.size,
                         shaURL: sha?.browser_download_url)
    }

    static func sha256(of url: URL) throws -> String {
        let h = try FileHandle(forReadingFrom: url)
        defer { try? h.close() }
        var hasher = SHA256()
        while let chunk = try h.read(upToCount: 1 << 20), !chunk.isEmpty {
            hasher.update(data: chunk)
        }
        return hasher.finalize().map { String(format: "%02x", $0) }.joined()
    }

    private static func describe(_ error: Error) -> String {
        if let u = error as? URLError {
            switch u.code {
            case .notConnectedToInternet, .networkConnectionLost: return "No internet connection."
            case .timedOut: return "GitHub did not answer in time."
            case .cannotFindHost, .cannotConnectToHost: return "Cannot reach github.com."
            default: break
            }
        }
        if error is DecodingError { return "Unexpected answer from GitHub." }
        return error.localizedDescription
    }
}
