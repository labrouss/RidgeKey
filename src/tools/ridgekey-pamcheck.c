/* ridgekey-pamcheck - run a PAM auth stack once and report what happened.
 * SPDX-License-Identifier: MIT
 *
 * Used by ridgekey-sudo before it touches /etc/pam.d/sudo_local (to prove OpenPAM can load
 * pam_ridgekey.so) and by CI. Prompts are answered with an empty string; info text is printed.
 *
 *   ridgekey-pamcheck SERVICE USER
 *
 * Exit: 0 = pam_start + pam_authenticate ran (result printed), 1 = PAM could not start the
 * service (e.g. a module failed to load), 2 = usage.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <security/pam_appl.h>

static int
conv_fn (int n, const struct pam_message **msg, struct pam_response **resp, void *data)
{
  struct pam_response *r = calloc ((size_t) n, sizeof *r);
  int i;

  (void) data;
  if (!r)
    return PAM_BUF_ERR;
  for (i = 0; i < n; i++)
    {
      switch (msg[i]->msg_style)
        {
        case PAM_PROMPT_ECHO_OFF:
        case PAM_PROMPT_ECHO_ON:
          printf ("[prompt] %s -> (empty)\n", msg[i]->msg);
          r[i].resp = strdup ("");
          break;
        default:
          printf ("[info] %s\n", msg[i]->msg);
          break;
        }
    }
  fflush (stdout);
  *resp = r;
  return PAM_SUCCESS;
}

static const char *
code_name (int rc)
{
  switch (rc)
    {
    case PAM_SUCCESS:          return "PAM_SUCCESS";
    case PAM_AUTH_ERR:         return "PAM_AUTH_ERR";
    case PAM_IGNORE:           return "PAM_IGNORE";
    case PAM_SYSTEM_ERR:       return "PAM_SYSTEM_ERR";
    case PAM_OPEN_ERR:         return "PAM_OPEN_ERR";
    case PAM_SYMBOL_ERR:       return "PAM_SYMBOL_ERR";
    case PAM_USER_UNKNOWN:     return "PAM_USER_UNKNOWN";
    case PAM_AUTHINFO_UNAVAIL: return "PAM_AUTHINFO_UNAVAIL";
    case PAM_PERM_DENIED:      return "PAM_PERM_DENIED";
    case PAM_BUF_ERR:          return "PAM_BUF_ERR";
    default:                   return "PAM_OTHER";
    }
}

int
main (int argc, char **argv)
{
  struct pam_conv conv = { conv_fn, NULL };
  pam_handle_t *pamh = NULL;
  int rc;

  if (argc != 3)
    {
      fprintf (stderr, "usage: %s SERVICE USER\n", argv[0]);
      return 2;
    }
  rc = pam_start (argv[1], argv[2], &conv, &pamh);
  if (rc != PAM_SUCCESS)
    {
      printf ("pam_start: %s\nresult=%s\n", pam_strerror (pamh, rc), code_name (rc));
      return 1;
    }
  rc = pam_authenticate (pamh, 0);
  printf ("pam_authenticate: %s\nresult=%s\n", pam_strerror (pamh, rc), code_name (rc));
  pam_end (pamh, rc);
  return 0;
}
