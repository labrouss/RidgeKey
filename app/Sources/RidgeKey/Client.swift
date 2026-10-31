// RidgeKey - XPC client for ridgekeyd (mirror of src/common/ridgekey-protocol.h).
// SPDX-License-Identifier: MIT
import Foundation
@preconcurrency import XPC   // XPC objects predate Sendable; they are thread-safe

enum RK {
    static let service = "io.github.labrouss.ridgekey"
    static let agentPath = "/usr/local/ridgekey/RidgeKeyAgent.app"
    static let sudoTool = "/usr/local/ridgekey/bin/ridgekey-sudo"
    static let adminTool = "/usr/local/ridgekey/bin/ridgekey-admin"
    // documentation (GitHub Pages, built from docs/)
    static let docs = URL(string: "https://labrouss.github.io/RidgeKey/")!
    static let repo = URL(string: "https://github.com/labrouss/RidgeKey")!
    static let newIssue = URL(string: "https://github.com/labrouss/RidgeKey/issues/new")!
    static func docs(_ page: String) -> URL {
        page.isEmpty ? docs : docs.appendingPathComponent(page + ".html")
    }
    static let fingerprintRule = "io.github.labrouss.ridgekey.fingerprint"

    static let fingers = [
        "right-index", "right-thumb", "right-middle", "right-ring", "right-little",
        "left-index", "left-thumb", "left-middle", "left-ring", "left-little",
    ]

    /// "right-index" -> "Right index"
    static func label(_ finger: String) -> String {
        let parts = finger.split(separator: "-").map(String.init)
        guard parts.count == 2 else { return finger }
        return parts[0].capitalized + " " + parts[1]
    }
}

/// A reply from ridgekeyd (or a stand-in when the daemon cannot be reached).
struct RKReply {
    let reachable: Bool
    let fields: [String: Any]

    var ok: Bool { fields["ok"] as? Bool ?? false }
    var error: String? { fields["error"] as? String }
    var message: String {
        if !reachable { return "ridgekeyd is not running - is RidgeKey installed?" }
        return fields["message"] as? String ?? (ok ? "" : "unknown error")
    }
    func string(_ key: String) -> String? { fields[key] as? String }
    func bool(_ key: String) -> Bool { fields[key] as? Bool ?? false }
    func int(_ key: String) -> Int { fields[key] as? Int ?? 0 }
    func strings(_ key: String) -> [String] { (fields[key] as? [Any])?.compactMap { $0 as? String } ?? [] }

    static let unreachable = RKReply(reachable: false, fields: [:])
}

/// A running request; cancel() asks ridgekeyd to stop it (e.g. an enrollment).
final class RKOperation {
    fileprivate let connection: xpc_connection_t
    fileprivate init(connection: xpc_connection_t) { self.connection = connection }

    func cancel() {
        let msg = xpc_dictionary_create(nil, nil, 0)
        xpc_dictionary_set_string(msg, "op", "cancel")
        xpc_connection_send_message(connection, msg)
    }
}

enum RKClient {
    /// Send one request and wait for the reply. Events (finger, enroll-progress, retry) are
    /// delivered on the main actor while the request runs.
    static func call(_ request: [String: Any],
                     onStart: (@MainActor (RKOperation) -> Void)? = nil,
                     onEvent: (@MainActor ([String: Any]) -> Void)? = nil) async -> RKReply {
        await withCheckedContinuation { continuation in
            let queue = DispatchQueue(label: "io.github.labrouss.ridgekey.app.xpc")
            let connection = xpc_connection_create_mach_service(
                RK.service, queue, UInt64(XPC_CONNECTION_MACH_SERVICE_PRIVILEGED))

            xpc_connection_set_event_handler(connection) { event in
                guard xpc_get_type(event) == XPC_TYPE_DICTIONARY, let onEvent = onEvent else { return }
                let fields = toSwift(event)
                Task { @MainActor in onEvent(fields) }
            }
            xpc_connection_resume(connection)

            if let onStart = onStart {
                let op = RKOperation(connection: connection)
                Task { @MainActor in onStart(op) }
            }

            let msg = toXPC(request)

            xpc_connection_send_message_with_reply(connection, msg, queue) { reply in
                let result: RKReply
                if xpc_get_type(reply) == XPC_TYPE_DICTIONARY {
                    result = RKReply(reachable: true, fields: toSwift(reply))
                } else {
                    result = .unreachable
                }
                xpc_connection_cancel(connection)
                continuation.resume(returning: result)
            }
        }
    }

    /// Bool, Int, Double, String, [Any] and nested [String: Any] (finger-action rules).
    private static func toXPC(_ dict: [String: Any]) -> xpc_object_t {
        let msg = xpc_dictionary_create(nil, nil, 0)
        for (key, value) in dict {
            if let v = toXPCValue(value) { xpc_dictionary_set_value(msg, key, v) }
        }
        return msg
    }

    private static func toXPCValue(_ value: Any) -> xpc_object_t? {
        switch value {
        case let v as Bool: return xpc_bool_create(v)
        case let v as Int: return xpc_int64_create(Int64(v))
        case let v as Double: return xpc_double_create(v)
        case let v as String: return xpc_string_create(v)
        case let v as [String: Any]: return toXPC(v)
        case let v as [Any]:
            let arr = xpc_array_create(nil, 0)
            for item in v { if let x = toXPCValue(item) { xpc_array_append_value(arr, x) } }
            return arr
        default: return nil
        }
    }

    private static func toSwift(_ dict: xpc_object_t) -> [String: Any] {
        var out: [String: Any] = [:]
        xpc_dictionary_apply(dict) { key, value in
            if let v = convert(value) { out[String(cString: key)] = v }
            return true
        }
        return out
    }

    private static func convert(_ value: xpc_object_t) -> Any? {
        let type = xpc_get_type(value)
        if type == XPC_TYPE_STRING, let p = xpc_string_get_string_ptr(value) { return String(cString: p) }
        if type == XPC_TYPE_BOOL { return xpc_bool_get_value(value) }
        if type == XPC_TYPE_INT64 { return Int(xpc_int64_get_value(value)) }
        if type == XPC_TYPE_DOUBLE { return xpc_double_get_value(value) }
        if type == XPC_TYPE_DICTIONARY { return toSwift(value) }
        if type == XPC_TYPE_ARRAY {
            var items: [Any] = []
            xpc_array_apply(value) { _, item in
                if let v = convert(item) { items.append(v) }
                return true
            }
            return items
        }
        return nil
    }
}
