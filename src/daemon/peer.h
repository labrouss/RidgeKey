/* RidgeKey - identify the process on the other end of an XPC connection.
 * SPDX-License-Identifier: MIT
 */
#ifndef RIDGEKEY_PEER_H
#define RIDGEKEY_PEER_H

#include <sys/types.h>
#include <glib.h>
#include <xpc/xpc.h>

/* TRUE when the peer is the installed RidgeKey agent:
 *   - its executable is RK_AGENT_PATH (resolved from the connection's audit token, so a
 *     reused pid cannot be confused with it),
 *   - its code signature is valid right now (no in-memory modification),
 *   - it is signed with the hardened runtime (no DYLD injection, no debugger attach without
 *     root), and
 *   - RK_AGENT_PATH and every directory above it are owned by root and not group/world
 *     writable.
 * *euid receives the peer's effective uid. */
gboolean peer_is_agent (xpc_connection_t conn, uid_t *euid, pid_t *pid, GError **error);

#endif /* RIDGEKEY_PEER_H */
