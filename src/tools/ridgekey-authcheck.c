/* ridgekey-authcheck - ask authd for one right, without any user interaction.
 * SPDX-License-Identifier: MIT
 *
 *   ridgekey-authcheck <right>      exit 0 = granted, 1 = denied, 2 = error / usage
 *
 * ridgekey-admin runs it (as root) on io.github.labrouss.ridgekey.selftest, whose only
 * mechanism is RidgeKey:selftest,privileged. Success proves macOS can load and run
 * RidgeKey.bundle - checked before any real admin right is changed, and after every install.
 */
#include <stdio.h>
#include <string.h>

#include <Security/Authorization.h>

int
main (int argc, char **argv)
{
  AuthorizationRef auth = NULL;
  AuthorizationItem item = { 0 };
  AuthorizationRights rights = { 1, &item };
  OSStatus st;

  if (argc != 2 || argv[1][0] == '-')
    {
      fprintf (stderr, "usage: ridgekey-authcheck <right>\n");
      return 2;
    }
  item.name = argv[1];

  st = AuthorizationCreate (NULL, kAuthorizationEmptyEnvironment, kAuthorizationFlagDefaults, &auth);
  if (st != errAuthorizationSuccess)
    {
      fprintf (stderr, "AuthorizationCreate failed: %d\n", (int) st);
      return 2;
    }
  st = AuthorizationCopyRights (auth, &rights, kAuthorizationEmptyEnvironment,
                                kAuthorizationFlagExtendRights | kAuthorizationFlagDestroyRights, NULL);
  AuthorizationFree (auth, kAuthorizationFlagDefaults);

  switch (st)
    {
    case errAuthorizationSuccess:
      printf ("%s: granted\n", argv[1]);
      return 0;
    case errAuthorizationDenied:
    case errAuthorizationInteractionNotAllowed:
    case errAuthorizationCanceled:
      printf ("%s: denied (%d)\n", argv[1], (int) st);
      return 1;
    default:
      printf ("%s: error %d\n", argv[1], (int) st);
      return 2;
    }
}
