/* ridgekeyctl - command-line client for ridgekeyd
 * SPDX-License-Identifier: MIT
 *
 * Exit codes: 0 success / match, 1 failed / no match, 2 usage, 3 cannot reach ridgekeyd.
 */
#define __STDC_WANT_LIB_EXT1__ 1   /* memset_s */
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <readpassphrase.h>
#include <xpc/xpc.h>
#include <dispatch/dispatch.h>

#include "ridgekey-protocol.h"

static xpc_connection_t conn;
static dispatch_semaphore_t done;
static int exit_code = 1;
static int verbose = 0;

static void
usage (FILE *f)
{
  int i;

  fprintf (f,
    "usage: ridgekeyctl [-v] [-u USER] COMMAND\n"
    "\n"
    "  status                  sensor and daemon status\n"
    "  list                    fingers RidgeKey has enrolled for the user\n"
    "  enroll FINGER           enroll a finger (replaces an earlier RidgeKey print of it)\n"
    "  verify [-t SECONDS]     match against the user's prints (exit 0 = match)\n"
    "  identify [-t SECONDS]   match against every user's prints (root only)\n"
    "  delete FINGER           delete one RidgeKey print from the sensor and the record\n"
    "  purge [--yes]           delete every RidgeKey print, one by one (root only)\n"
    "  password set            store your login password for lock-screen unlock\n"
    "                          (checked against your account, kept in the System keychain)\n"
    "  password clear          remove the stored password\n"
    "  password check          is a password stored, and is it still your login password?\n"
    "                          (password set --stdin reads it from standard input, for scripts)\n"
    "  authorize [RIGHT]       test the admin-dialog path: on-screen prompt + an admin's finger\n"
    "                          (root only; the password is never printed)\n"
    "\n"
    "  -u USER   act on another user (root only)\n"
    "  -v        show finger on/off events\n"
    "\n"
    "FINGER:");
  for (i = RK_FINGER_FIRST; i <= RK_FINGER_LAST; i++)
    fprintf (f, " %s", rk_finger_names[i]);
  fprintf (f, "\n\nRidgeKey never erases the whole sensor; Windows Hello / BIOS prints are left alone.\n");
}

static void
print_failure (xpc_object_t reply)
{
  const char *code = xpc_dictionary_get_string (reply, RK_KEY_ERROR);
  const char *msg = xpc_dictionary_get_string (reply, RK_KEY_MESSAGE);

  fprintf (stderr, "error: %s%s%s%s\n", msg ? msg : "unknown error",
           code ? " [" : "", code ? code : "", code ? "]" : "");
}

static void
handle_event (xpc_object_t ev)
{
  const char *name = xpc_dictionary_get_string (ev, RK_KEY_EVENT);

  if (!name)
    return;
  if (strcmp (name, RK_EV_ENROLL_PROGRESS) == 0)
    {
      long long stage = xpc_dictionary_get_int64 (ev, RK_KEY_STAGE);
      long long stages = xpc_dictionary_get_int64 (ev, RK_KEY_STAGES);

      printf ("  scan %lld/%lld ok%s\n", stage, stages, stage < stages ? " - lift and touch again" : "");
    }
  else if (strcmp (name, RK_EV_RETRY) == 0)
    {
      printf ("  retry: %s\n", xpc_dictionary_get_string (ev, RK_KEY_MESSAGE));
    }
  else if (strcmp (name, RK_EV_FINGER) == 0 && verbose)
    {
      printf ("  finger %s\n", xpc_dictionary_get_bool (ev, RK_KEY_PRESENT) ? "on sensor" : "lifted");
    }
  fflush (stdout);
}

static void
print_result (const char *op, xpc_object_t reply)
{
  if (!xpc_dictionary_get_bool (reply, RK_KEY_OK))
    {
      print_failure (reply);
      exit_code = 1;
      return;
    }
  exit_code = 0;

  if (strcmp (op, RK_OP_STATUS) == 0)
    {
      const char *dev = xpc_dictionary_get_string (reply, RK_KEY_DEVICE);

      const char *cu = xpc_dictionary_get_string (reply, RK_KEY_CONSOLE_USER);

      printf ("ridgekeyd %s%s\n", xpc_dictionary_get_string (reply, RK_KEY_VERSION),
              xpc_dictionary_get_bool (reply, RK_KEY_BUSY) ? " (busy)" : "");
      if (cu)
        printf ("console:  %s%s\n", *cu ? cu : "login window",
                xpc_dictionary_get_bool (reply, RK_KEY_SCREEN_LOCKED) ? " (screen locked)" : "");
      if (dev && *dev)
        printf ("sensor:   %s (driver %s, id %s, %lld enroll scans)\n", dev,
                xpc_dictionary_get_string (reply, RK_KEY_DRIVER),
                xpc_dictionary_get_string (reply, RK_KEY_DEVICE_ID),
                (long long) xpc_dictionary_get_int64 (reply, RK_KEY_STAGES));
      else
        printf ("sensor:   none found\n");
    }
  else if (strcmp (op, RK_OP_LIST) == 0)
    {
      xpc_object_t arr = xpc_dictionary_get_value (reply, RK_KEY_FINGERS);
      size_t i, n = arr ? xpc_array_get_count (arr) : 0;

      printf ("%s: %zu finger%s enrolled\n", xpc_dictionary_get_string (reply, RK_KEY_USER), n,
              n == 1 ? "" : "s");
      for (i = 0; i < n; i++)
        printf ("  %s\n", xpc_array_get_string (arr, i));
      printf ("lock-screen password: %s\n",
              xpc_dictionary_get_bool (reply, RK_KEY_PASSWORD_STORED) ? "stored" : "not stored");
    }
  else if (strcmp (op, RK_OP_ENROLL) == 0)
    {
      printf ("enrolled %s\n", xpc_dictionary_get_string (reply, RK_KEY_FINGER));
    }
  else if (strcmp (op, RK_OP_VERIFY) == 0 || strcmp (op, RK_OP_IDENTIFY) == 0)
    {
      if (xpc_dictionary_get_bool (reply, RK_KEY_MATCHED))
        printf ("MATCH: %s / %s\n", xpc_dictionary_get_string (reply, RK_KEY_USER),
                xpc_dictionary_get_string (reply, RK_KEY_FINGER));
      else
        {
          printf ("NO MATCH\n");
          exit_code = 1;
        }
    }
  else if (strcmp (op, RK_OP_DELETE) == 0)
    {
      printf ("deleted %s\n", xpc_dictionary_get_string (reply, RK_KEY_FINGER));
    }
  else if (strcmp (op, RK_OP_PURGE) == 0)
    {
      printf ("deleted %lld RidgeKey print(s)\n", (long long) xpc_dictionary_get_int64 (reply, RK_KEY_DELETED));
    }
  else if (strcmp (op, RK_OP_PASSWORD_SET) == 0)
    {
      printf ("Password checked and stored for %s.\n", xpc_dictionary_get_string (reply, RK_KEY_USER));
    }
  else if (strcmp (op, RK_OP_PASSWORD_CLEAR) == 0)
    {
      printf ("Stored password removed for %s.\n", xpc_dictionary_get_string (reply, RK_KEY_USER));
    }
  else if (strcmp (op, RK_OP_AUTHORIZE) == 0)
    {
      printf ("APPROVED: administrator %s%s (password released to this process - not shown)\n",
              xpc_dictionary_get_string (reply, RK_KEY_USER),
              xpc_dictionary_get_bool (reply, RK_KEY_REUSED) ? ", from the previous touch" : "");
    }
  else if (strcmp (op, RK_OP_PASSWORD_CHECK) == 0)
    {
      int stored = xpc_dictionary_get_bool (reply, RK_KEY_PASSWORD_STORED);
      int valid = xpc_dictionary_get_bool (reply, RK_KEY_PASSWORD_VALID);

      printf ("%s: %s\n", xpc_dictionary_get_string (reply, RK_KEY_USER),
              !stored ? "no password stored" :
              valid ? "stored password is valid" :
              "stored password is OUTDATED - run: ridgekeyctl password set");
      exit_code = (stored && valid) ? 0 : 1;
    }
}

static void
send_cancel (void)
{
  xpc_object_t msg = xpc_dictionary_create (NULL, NULL, 0);

  xpc_dictionary_set_string (msg, RK_KEY_OP, RK_OP_CANCEL);
  xpc_connection_send_message (conn, msg);
  xpc_release (msg);
}

int
main (int argc, char **argv)
{
  const char *user = NULL, *op, *finger = NULL;
  long timeout = 0;
  int yes = 0, from_stdin = 0, opt;
  char password[1024] = "";
  xpc_object_t msg;
  dispatch_queue_t q;
  dispatch_source_t sigint;

  while ((opt = getopt (argc, argv, "+u:vh")) != -1)
    switch (opt)
      {
      case 'u': user = optarg; break;
      case 'v': verbose = 1; break;
      case 'h': usage (stdout); return 0;
      default:  usage (stderr); return 2;
      }
  if (optind >= argc)
    {
      usage (stderr);
      return 2;
    }
  op = argv[optind++];

  /* command arguments */
  for (; optind < argc; optind++)
    {
      const char *a = argv[optind];

      if (strcmp (a, "-t") == 0 && optind + 1 < argc)
        timeout = strtol (argv[++optind], NULL, 10);
      else if (strcmp (a, "--yes") == 0)
        yes = 1;
      else if (strcmp (a, "--stdin") == 0)
        from_stdin = 1;
      else if (!finger)
        finger = a;
      else
        {
          fprintf (stderr, "unexpected argument: %s\n", a);
          return 2;
        }
    }

  if (strcmp (op, "password") == 0)
    {
      if (finger && strcmp (finger, "set") == 0)
        op = RK_OP_PASSWORD_SET;
      else if (finger && strcmp (finger, "clear") == 0)
        op = RK_OP_PASSWORD_CLEAR;
      else if (finger && strcmp (finger, "check") == 0)
        op = RK_OP_PASSWORD_CHECK;
      else
        {
          fprintf (stderr, "usage: ridgekeyctl password set|clear|check\n");
          return 2;
        }
      finger = NULL;
    }
  if (strcmp (op, RK_OP_PASSWORD_SET) == 0)
    {
      if (from_stdin)
        {
          size_t n;

          if (!fgets (password, sizeof password, stdin))
            password[0] = '\0';
          n = strlen (password);
          if (n && password[n - 1] == '\n')
            password[n - 1] = '\0';
        }
      else
        {
          printf ("RidgeKey types this password into the lock screen after a fingerprint match.\n"
                  "It is checked against your account and stored in the System keychain (root only).\n");
          if (!readpassphrase ("Login password: ", password, sizeof password, RPP_ECHO_OFF | RPP_REQUIRE_TTY))
            password[0] = '\0';
        }
      if (!*password)
        {
          fprintf (stderr, "no password entered\n");
          return 2;
        }
    }

  if (strcmp (op, RK_OP_STATUS) && strcmp (op, RK_OP_LIST) && strcmp (op, RK_OP_ENROLL) &&
      strcmp (op, RK_OP_VERIFY) && strcmp (op, RK_OP_IDENTIFY) && strcmp (op, RK_OP_DELETE) &&
      strcmp (op, RK_OP_PURGE) && strcmp (op, RK_OP_PASSWORD_SET) && strcmp (op, RK_OP_PASSWORD_CLEAR) &&
      strcmp (op, RK_OP_PASSWORD_CHECK) && strcmp (op, RK_OP_UNLOCK) && strcmp (op, RK_OP_AUTHORIZE))   /* unlock: test only, always refused */
    {
      fprintf (stderr, "unknown command: %s\n\n", op);
      usage (stderr);
      return 2;
    }
  if ((strcmp (op, RK_OP_ENROLL) == 0 || strcmp (op, RK_OP_DELETE) == 0) && !rk_finger_from_name (finger))
    {
      fprintf (stderr, "%s needs a finger name\n\n", op);
      usage (stderr);
      return 2;
    }
  if (strcmp (op, RK_OP_PURGE) == 0 && !yes)
    {
      char line[16] = "";

      printf ("Delete EVERY print RidgeKey enrolled (all users) from the sensor and the record?\n"
              "Windows Hello / BIOS prints are not touched. Type 'yes' to continue: ");
      fflush (stdout);
      if (!fgets (line, sizeof line, stdin) || strncmp (line, "yes", 3) != 0)
        return 1;
    }

  q = dispatch_queue_create ("ridgekeyctl", DISPATCH_QUEUE_SERIAL);
  done = dispatch_semaphore_create (0);
  conn = xpc_connection_create_mach_service (RK_SERVICE_NAME, q, XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
  xpc_connection_set_event_handler (conn, ^(xpc_object_t ev) {
    if (xpc_get_type (ev) == XPC_TYPE_DICTIONARY)
      handle_event (ev);
  });
  xpc_connection_resume (conn);

  /* Ctrl-C asks the daemon to cancel; the reply to the original request then ends the program. */
  signal (SIGINT, SIG_IGN);
  sigint = dispatch_source_create (DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0, q);
  dispatch_source_set_event_handler (sigint, ^{
    fprintf (stderr, "\ncancelling...\n");
    send_cancel ();
  });
  dispatch_resume (sigint);

  msg = xpc_dictionary_create (NULL, NULL, 0);
  xpc_dictionary_set_string (msg, RK_KEY_OP, op);
  if (user)
    xpc_dictionary_set_string (msg, RK_KEY_USER, user);
  if (finger && strcmp (op, RK_OP_AUTHORIZE) == 0)
    {
      xpc_dictionary_set_string (msg, RK_KEY_RIGHT, finger);
      xpc_dictionary_set_string (msg, RK_KEY_CLIENT, "/usr/local/bin/ridgekeyctl");
      finger = NULL;
    }
  else if (strcmp (op, RK_OP_AUTHORIZE) == 0)
    xpc_dictionary_set_string (msg, RK_KEY_RIGHT, "ridgekeyctl test");
  if (finger)
    xpc_dictionary_set_string (msg, RK_KEY_FINGER, finger);
  if (timeout > 0)
    xpc_dictionary_set_int64 (msg, RK_KEY_TIMEOUT, timeout);
  if (*password)
    {
      xpc_dictionary_set_string (msg, RK_KEY_PASSWORD, password);
      memset_s (password, sizeof password, 0, sizeof password);
    }

  if (strcmp (op, RK_OP_ENROLL) == 0)
    printf ("Enrolling %s - touch the sensor, lift, and repeat until done.\n", finger);
  else if (strcmp (op, RK_OP_VERIFY) == 0 || strcmp (op, RK_OP_IDENTIFY) == 0 ||
           strcmp (op, RK_OP_AUTHORIZE) == 0)
    printf ("Touch the sensor...\n");
  fflush (stdout);

  xpc_connection_send_message_with_reply (conn, msg, q, ^(xpc_object_t reply) {
    if (xpc_get_type (reply) == XPC_TYPE_DICTIONARY)
      print_result (op, reply);
    else
      {
        fprintf (stderr, "error: cannot reach ridgekeyd (%s). Is it installed and running?\n"
                         "       sudo launchctl print system/%s\n",
                 reply == XPC_ERROR_CONNECTION_INVALID ? "service not found" : "connection interrupted",
                 RK_SERVICE_NAME);
        exit_code = 3;
      }
    dispatch_semaphore_signal (done);
  });
  xpc_release (msg);

  dispatch_semaphore_wait (done, DISPATCH_TIME_FOREVER);
  xpc_connection_cancel (conn);
  return exit_code;
}
