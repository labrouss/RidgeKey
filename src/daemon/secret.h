/* RidgeKey - the user's login password, for typing into the lock screen after a match.
 * SPDX-License-Identifier: MIT
 *
 * Stored as one generic-password item per user in /Library/Keychains/System.keychain
 * (service RK_SECRET_SERVICE, account = short user name). The System keychain is encrypted at
 * rest and readable only by root. The item's ACL allows any root process without a prompt, so
 * an upgraded (re-signed) ridgekeyd keeps access - root could read the keychain file anyway.
 *
 * Passwords are checked against the real account (PAM service "checkpw") before being stored,
 * and again before every use, so a changed password is never typed.
 */
#ifndef RIDGEKEY_SECRET_H
#define RIDGEKEY_SECRET_H

#include <glib.h>

#define RK_SECRET_SERVICE  "io.github.labrouss.ridgekey.password"
#define RK_SECRET_KEYCHAIN "/Library/Keychains/System.keychain"

/* TRUE if password is the current login password of user. */
gboolean secret_validate (const char *user, const char *password, GError **error);

gboolean secret_store (const char *user, const char *password, GError **error);
gboolean secret_clear (const char *user, GError **error);
gboolean secret_exists (const char *user);

/* Returns a newly allocated copy (free with secret_free), or NULL with error. */
char    *secret_load (const char *user, GError **error);
void     secret_free (char *password);

#endif /* RIDGEKEY_SECRET_H */
