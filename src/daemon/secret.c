/* RidgeKey - the user's login password in the System keychain.
 * SPDX-License-Identifier: MIT
 *
 * Uses the file-based keychain API (SecKeychain*), which is deprecated but is still the only
 * C API for the System keychain with custom ACLs.
 */
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#define __STDC_WANT_LIB_EXT1__ 1   /* memset_s */

#include "secret.h"

#include <stdlib.h>
#include <string.h>
#include <gio/gio.h>
#include <Security/Security.h>
#include <security/pam_appl.h>

static void
set_os_error (GError **error, OSStatus st, const char *what)
{
  CFStringRef s = SecCopyErrorMessageString (st, NULL);
  char buf[256] = "";

  if (s)
    {
      CFStringGetCString (s, buf, sizeof buf, kCFStringEncodingUTF8);
      CFRelease (s);
    }
  g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s: %s (%d)", what, buf, (int) st);
}

static SecKeychainRef
open_keychain (GError **error)
{
  static gsize once = 0;
  SecKeychainRef kc = NULL;
  OSStatus st;

  if (g_once_init_enter (&once))
    {
      SecKeychainSetUserInteractionAllowed (false);   /* a daemon can never answer a prompt */
      g_once_init_leave (&once, 1);
    }
  st = SecKeychainOpen (RK_SECRET_KEYCHAIN, &kc);
  if (st != errSecSuccess)
    {
      set_os_error (error, st, "cannot open the System keychain");
      return NULL;
    }
  return kc;
}

static OSStatus
find_item (SecKeychainRef kc, const char *user, SecKeychainItemRef *item, UInt32 *len, void **data)
{
  return SecKeychainFindGenericPassword (kc, (UInt32) strlen (RK_SECRET_SERVICE), RK_SECRET_SERVICE,
                                         (UInt32) strlen (user), user, len, data, item);
}

/* ACL: decrypt allowed for any application (all callers are root anyway), never prompt. */
static SecAccessRef
make_access (void)
{
  SecAccessRef access = NULL;
  CFArrayRef acls;
  CFIndex i;

  if (SecAccessCreate (CFSTR ("RidgeKey stored login password"), NULL, &access) != errSecSuccess)
    return NULL;
  acls = SecAccessCopyMatchingACLList (access, kSecACLAuthorizationDecrypt);
  if (acls)
    {
      for (i = 0; i < CFArrayGetCount (acls); i++)
        SecACLSetContents ((SecACLRef) CFArrayGetValueAtIndex (acls, i), NULL,
                           CFSTR ("RidgeKey"), 0);
      CFRelease (acls);
    }
  return access;
}

gboolean
secret_exists (const char *user)
{
  g_autoptr(GError) err = NULL;
  SecKeychainRef kc = open_keychain (&err);
  SecKeychainItemRef item = NULL;
  OSStatus st;

  if (!kc)
    return FALSE;
  st = find_item (kc, user, &item, NULL, NULL);
  if (item)
    CFRelease (item);
  CFRelease (kc);
  return st == errSecSuccess;
}

char *
secret_load (const char *user, GError **error)
{
  SecKeychainRef kc = open_keychain (error);
  SecKeychainItemRef item = NULL;
  UInt32 len = 0;
  void *data = NULL;
  char *copy;
  OSStatus st;

  if (!kc)
    return NULL;
  st = find_item (kc, user, &item, &len, &data);
  CFRelease (kc);
  if (item)
    CFRelease (item);
  if (st != errSecSuccess)
    {
      if (st == errSecItemNotFound)
        g_set_error (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "no stored password for %s", user);
      else
        set_os_error (error, st, "cannot read the stored password");
      return NULL;
    }
  copy = g_malloc (len + 1);
  memcpy (copy, data, len);
  copy[len] = '\0';
  memset (data, 0, len);
  SecKeychainItemFreeContent (NULL, data);
  return copy;
}

void
secret_free (char *password)
{
  if (!password)
    return;
  memset_s (password, strlen (password), 0, strlen (password));
  g_free (password);
}

gboolean
secret_clear (const char *user, GError **error)
{
  SecKeychainRef kc = open_keychain (error);
  SecKeychainItemRef item = NULL;
  OSStatus st;

  if (!kc)
    return FALSE;
  st = find_item (kc, user, &item, NULL, NULL);
  CFRelease (kc);
  if (st == errSecItemNotFound)
    return TRUE;
  if (st != errSecSuccess)
    {
      set_os_error (error, st, "cannot find the stored password");
      return FALSE;
    }
  st = SecKeychainItemDelete (item);
  CFRelease (item);
  if (st != errSecSuccess)
    {
      set_os_error (error, st, "cannot delete the stored password");
      return FALSE;
    }
  return TRUE;
}

gboolean
secret_store (const char *user, const char *password, GError **error)
{
  SecKeychainRef kc;
  SecAccessRef access;
  SecKeychainItemRef item = NULL;
  const char *label = "RidgeKey - login password for lock-screen unlock";
  SecKeychainAttribute attrs[3];
  SecKeychainAttributeList list;
  OSStatus st;

  if (!secret_clear (user, error))
    return FALSE;
  kc = open_keychain (error);
  if (!kc)
    return FALSE;
  access = make_access ();
  if (!access)
    {
      CFRelease (kc);
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "cannot create the keychain item ACL");
      return FALSE;
    }

  attrs[0].tag = kSecServiceItemAttr;
  attrs[0].length = (UInt32) strlen (RK_SECRET_SERVICE);
  attrs[0].data = (void *) RK_SECRET_SERVICE;
  attrs[1].tag = kSecAccountItemAttr;
  attrs[1].length = (UInt32) strlen (user);
  attrs[1].data = (void *) user;
  attrs[2].tag = kSecLabelItemAttr;
  attrs[2].length = (UInt32) strlen (label);
  attrs[2].data = (void *) label;
  list.count = 3;
  list.attr = attrs;

  st = SecKeychainItemCreateFromContent (kSecGenericPasswordItemClass, &list,
                                         (UInt32) strlen (password), password, kc, access, &item);
  CFRelease (access);
  CFRelease (kc);
  if (item)
    CFRelease (item);
  if (st != errSecSuccess)
    {
      set_os_error (error, st, "cannot store the password");
      return FALSE;
    }
  return TRUE;
}

/* ------------------------------------------------------------------------------------------ */
/* Validation through PAM "checkpw" (pam_opendirectory)                                        */

static int
checkpw_conv (int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
  struct pam_response *r = calloc ((size_t) n, sizeof *r);
  int i;

  if (!r)
    return PAM_BUF_ERR;
  for (i = 0; i < n; i++)
    if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF || msg[i]->msg_style == PAM_PROMPT_ECHO_ON)
      r[i].resp = strdup ((const char *) data);
  *resp = r;
  return PAM_SUCCESS;
}

gboolean
secret_validate (const char *user, const char *password, GError **error)
{
  struct pam_conv conv = { checkpw_conv, (void *) password };
  pam_handle_t *pamh = NULL;
  int rc;

  if (!password || !*password)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "empty password");
      return FALSE;
    }
  rc = pam_start ("checkpw", user, &conv, &pamh);
  if (rc != PAM_SUCCESS)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "PAM checkpw: %s", pam_strerror (pamh, rc));
      return FALSE;
    }
  pam_set_item (pamh, PAM_AUTHTOK, password);
  rc = pam_authenticate (pamh, PAM_SILENT | PAM_DISALLOW_NULL_AUTHTOK);
  pam_end (pamh, rc);
  if (rc == PAM_SUCCESS)
    return TRUE;
  if (rc == PAM_AUTH_ERR)
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED, "that is not the login password of %s", user);
  else
    g_set_error (error, G_IO_ERROR, G_IO_ERROR_FAILED, "password check failed: %s", pam_strerror (NULL, rc));
  return FALSE;
}
