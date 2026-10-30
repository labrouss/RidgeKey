/* RidgeKey - identify the process on the other end of an XPC connection.
 * SPDX-License-Identifier: MIT
 */
#include "peer.h"
#include "ridgekey-protocol.h"

#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <bsm/libbsm.h>
#include <gio/gio.h>
#include <Security/Security.h>

/* Public since macOS 10.7 in libxpc but missing from the SDK headers; widely used. */
extern void xpc_connection_get_audit_token (xpc_connection_t connection, audit_token_t *token);

#define RK_CS_RUNTIME 0x10000   /* kSecCodeSignatureRuntime */

static gboolean
fail (GError **error, const char *msg)
{
  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, msg);
  return FALSE;
}

/* path and every parent directory: owned by root, not writable by group/other */
static gboolean
root_owned_chain (const char *path)
{
  char buf[PATH_MAX];
  struct stat st;
  char *slash;

  if (g_strlcpy (buf, path, sizeof buf) >= sizeof buf)
    return FALSE;
  for (;;)
    {
      if (lstat (buf, &st) != 0 || st.st_uid != 0 || (st.st_mode & (S_IWGRP | S_IWOTH)))
        return FALSE;
      if (S_ISLNK (st.st_mode))
        return FALSE;
      slash = strrchr (buf, '/');
      if (!slash || slash == buf)
        break;
      *slash = '\0';
    }
  return TRUE;
}

gboolean
peer_is_agent (xpc_connection_t conn, uid_t *euid, pid_t *pid, GError **error)
{
  audit_token_t token;
  CFDataRef token_data;
  CFDictionaryRef attrs;
  const void *keys[1];
  const void *vals[1];
  SecCodeRef code = NULL;
  SecStaticCodeRef static_code = NULL;
  CFURLRef url = NULL;
  CFDictionaryRef info = NULL;
  char path[PATH_MAX] = "";
  gboolean ok = FALSE;
  uint32_t flags = 0;

  xpc_connection_get_audit_token (conn, &token);
  if (euid)
    *euid = audit_token_to_euid (token);
  if (pid)
    *pid = audit_token_to_pid (token);

  token_data = CFDataCreate (NULL, (const UInt8 *) &token, sizeof token);
  keys[0] = kSecGuestAttributeAudit;
  vals[0] = token_data;
  attrs = CFDictionaryCreate (NULL, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
  CFRelease (token_data);

  if (SecCodeCopyGuestWithAttributes (NULL, attrs, kSecCSDefaultFlags, &code) != errSecSuccess)
    {
      fail (error, "cannot identify the calling process");
      goto out;
    }
  if (SecCodeCheckValidity (code, kSecCSDefaultFlags, NULL) != errSecSuccess)
    {
      fail (error, "the calling process's code signature is not valid");
      goto out;
    }
  if (SecCodeCopyStaticCode (code, kSecCSDefaultFlags, &static_code) != errSecSuccess ||
      SecCodeCopyPath (static_code, kSecCSDefaultFlags, &url) != errSecSuccess ||
      !CFURLGetFileSystemRepresentation (url, true, (UInt8 *) path, sizeof path))
    {
      fail (error, "cannot find the calling process's executable");
      goto out;
    }
  if (strcmp (path, RK_AGENT_PATH) != 0)
    {
      fail (error, "only the RidgeKey agent may unlock the screen");
      goto out;
    }
  if (SecCodeCopySigningInformation (static_code, kSecCSSigningInformation, &info) == errSecSuccess && info)
    {
      CFNumberRef f = CFDictionaryGetValue (info, kSecCodeInfoFlags);

      if (f)
        CFNumberGetValue (f, kCFNumberSInt32Type, &flags);
    }
  if (!(flags & RK_CS_RUNTIME))
    {
      fail (error, "the agent is not signed with the hardened runtime");
      goto out;
    }
  if (!root_owned_chain (RK_AGENT_PATH))
    {
      fail (error, "the agent executable or a parent directory is not root-owned and protected");
      goto out;
    }
  ok = TRUE;

out:
  if (info)
    CFRelease (info);
  if (url)
    CFRelease (url);
  if (static_code)
    CFRelease (static_code);
  if (code)
    CFRelease (code);
  CFRelease (attrs);
  return ok;
}
