/* RidgeKey - on-screen notice (see prompt.h).
 * SPDX-License-Identifier: MIT
 */
#define G_LOG_DOMAIN "ridgekeyd"

#include <string.h>

#include <CoreFoundation/CoreFoundation.h>

#include "prompt.h"

#define RK_PROMPT_ICON "/Applications/RidgeKey.app/Contents/Resources/RidgeKey.icns"

struct Prompt {
  CFUserNotificationRef note;
  CFStringRef           header;
  CFStringRef           button;
  guint                 timeout;
};

typedef struct {
  CFUserNotificationRef note;      /* own reference */
  GCancellable         *cancel;    /* own reference */
} Watch;

static CFStringRef
cfstr (const char *s)
{
  return CFStringCreateWithCString (kCFAllocatorDefault, s ? s : "", kCFStringEncodingUTF8);
}

static CFDictionaryRef
make_dict (CFStringRef header, const char *message, CFStringRef button)
{
  CFMutableDictionaryRef d = CFDictionaryCreateMutable (kCFAllocatorDefault, 0,
                                                        &kCFTypeDictionaryKeyCallBacks,
                                                        &kCFTypeDictionaryValueCallBacks);
  CFStringRef msg = cfstr (message);
  CFURLRef icon = CFURLCreateFromFileSystemRepresentation (kCFAllocatorDefault,
                                                           (const UInt8 *) RK_PROMPT_ICON,
                                                           (CFIndex) strlen (RK_PROMPT_ICON), false);

  CFDictionarySetValue (d, kCFUserNotificationAlertHeaderKey, header);
  CFDictionarySetValue (d, kCFUserNotificationAlertMessageKey, msg);
  CFDictionarySetValue (d, kCFUserNotificationDefaultButtonTitleKey, button);
  if (icon && g_file_test (RK_PROMPT_ICON, G_FILE_TEST_EXISTS))
    CFDictionarySetValue (d, kCFUserNotificationIconURLKey, icon);
  if (icon)
    CFRelease (icon);
  CFRelease (msg);
  return d;
}

/* Own thread: wait for the button (or for prompt_close, which answers "cancel"). */
static gpointer
watch_thread (gpointer data)
{
  Watch *w = data;
  CFOptionFlags flags = 0;

  if (CFUserNotificationReceiveResponse (w->note, 0, &flags) == 0 &&
      (flags & 0x3) == kCFUserNotificationDefaultResponse)
    {
      g_message ("prompt: '%s' pressed", "Use Password");
      g_cancellable_cancel (w->cancel);        /* thread-safe */
    }
  CFRelease (w->note);
  g_object_unref (w->cancel);
  g_free (w);
  return NULL;
}

Prompt *
prompt_show (const char *header, const char *message, const char *button,
             guint timeout, GCancellable *cancel)
{
  Prompt *p = g_new0 (Prompt, 1);
  CFDictionaryRef d;
  SInt32 err = 0;
  Watch *w;

  p->header = cfstr (header);
  p->button = cfstr (button);
  p->timeout = timeout;
  d = make_dict (p->header, message, p->button);
  p->note = CFUserNotificationCreate (kCFAllocatorDefault, (CFTimeInterval) timeout,
                                      kCFUserNotificationNoteAlertLevel, &err, d);
  CFRelease (d);
  if (!p->note)
    {
      g_warning ("prompt: could not show the notice (error %d)", (int) err);
      CFRelease (p->header);
      CFRelease (p->button);
      g_free (p);
      return NULL;
    }

  w = g_new0 (Watch, 1);
  w->note = (CFUserNotificationRef) CFRetain (p->note);
  w->cancel = g_object_ref (cancel);
  g_thread_unref (g_thread_new ("rk-prompt", watch_thread, w));
  return p;
}

void
prompt_update (Prompt *p, const char *message)
{
  CFDictionaryRef d;

  if (!p)
    return;
  d = make_dict (p->header, message, p->button);
  CFUserNotificationUpdate (p->note, (CFTimeInterval) p->timeout, kCFUserNotificationNoteAlertLevel, d);
  CFRelease (d);
}

void
prompt_close (Prompt *p)
{
  if (!p)
    return;
  CFUserNotificationCancel (p->note);   /* the watch thread then sees "cancel" and exits */
  CFRelease (p->note);
  CFRelease (p->header);
  CFRelease (p->button);
  g_free (p);
}
