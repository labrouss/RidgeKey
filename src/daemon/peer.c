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
  /* For a bundle, Security reports the bundle's path; accept the executable path as well. */
  if (strcmp (path, RK_AGENT_BUNDLE) != 0 && strcmp (path, RK_AGENT_PATH) != 0)
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

/* ------------------------------------------------------------------------------------------ */
/* trusted clients: the installed ridgekeyctl and RidgeKey.app                                 */

/* RK_TRUSTED_CLIENTS (written by install.sh, root-owned): one "PATH<TAB>REQUIREMENT" per line,
 * REQUIREMENT being the client's designated requirement at install time (for an ad-hoc build:
 * its cdhash, i.e. exactly that binary). */
gboolean
peer_is_trusted_client (xpc_connection_t conn, char **name, GError **error)
{
  audit_token_t token;
  CFDataRef token_data;
  CFDictionaryRef attrs;
  const void *keys[1];
  const void *vals[1];
  SecCodeRef code = NULL;
  SecStaticCodeRef static_code = NULL;
  SecRequirementRef req = NULL;
  CFURLRef url = NULL;
  char path[PATH_MAX] = "";
  g_autofree char *contents = NULL;
  g_auto(GStrv) lines = NULL;
  const char *requirement = NULL;
  gboolean ok = FALSE;
  guint i;

  if (!root_owned_chain (RK_TRUSTED_CLIENTS) ||
      !g_file_get_contents (RK_TRUSTED_CLIENTS, &contents, NULL, NULL))
    return fail (error, "the list of trusted RidgeKey clients is missing or not root-owned - reinstall RidgeKey");

  xpc_connection_get_audit_token (conn, &token);
  token_data = CFDataCreate (NULL, (const UInt8 *) &token, sizeof token);
  keys[0] = kSecGuestAttributeAudit;
  vals[0] = token_data;
  attrs = CFDictionaryCreate (NULL, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                              &kCFTypeDictionaryValueCallBacks);
  CFRelease (token_data);

  if (SecCodeCopyGuestWithAttributes (NULL, attrs, kSecCSDefaultFlags, &code) != errSecSuccess ||
      SecCodeCopyStaticCode (code, kSecCSDefaultFlags, &static_code) != errSecSuccess ||
      SecCodeCopyPath (static_code, kSecCSDefaultFlags, &url) != errSecSuccess ||
      !CFURLGetFileSystemRepresentation (url, true, (UInt8 *) path, sizeof path))
    {
      fail (error, "cannot identify the calling program");
      goto out;
    }

  lines = g_strsplit (contents, "\n", -1);
  for (i = 0; lines[i]; i++)
    {
      char *tab = strchr (lines[i], '\t');

      if (tab && (size_t) (tab - lines[i]) == strlen (path) && strncmp (lines[i], path, tab - lines[i]) == 0)
        {
          requirement = tab + 1;
          break;
        }
    }
  if (!requirement || !*requirement)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "%s is not a trusted RidgeKey client - use ridgekeyctl or the RidgeKey app", path);
      goto out;
    }
  {
    CFStringRef s = CFStringCreateWithCString (NULL, requirement, kCFStringEncodingUTF8);
    OSStatus st = s ? SecRequirementCreateWithString (s, kSecCSDefaultFlags, &req) : errSecParam;

    if (s)
      CFRelease (s);
    if (st != errSecSuccess)
      {
        fail (error, "the recorded signature of this client cannot be read - reinstall RidgeKey");
        goto out;
      }
  }
  /* The running code (not just the file) must satisfy the recorded requirement. */
  if (SecCodeCheckValidity (code, kSecCSDefaultFlags, req) != errSecSuccess)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                   "%s does not match the signature recorded at install - reinstall RidgeKey", path);
      goto out;
    }
  if (name)
    *name = g_path_get_basename (path);
  ok = TRUE;

out:
  if (req)
    CFRelease (req);
  if (url)
    CFRelease (url);
  if (static_code)
    CFRelease (static_code);
  if (code)
    CFRelease (code);
  CFRelease (attrs);
  return ok;
}
