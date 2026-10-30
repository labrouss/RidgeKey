/* RidgeKey - on-disk record of the prints RidgeKey enrolled.
 * SPDX-License-Identifier: MIT
 *
 * The Synaptics sensor cannot list its prints, so the host keeps the record:
 *   <root>/prints/<uid>/<finger-name>.print   (fp_print_serialize output)
 * Directories 0700 and files 0600, owned by root. Only prints in this record are ever
 * deleted from the sensor.
 */
#ifndef RIDGEKEY_STORE_H
#define RIDGEKEY_STORE_H

#include <sys/types.h>
#include <fprint.h>

#define RK_STORE_ROOT_DEFAULT "/Library/Application Support/RidgeKey"

/* Key under which store_load_* attach the owning uid to each FpPrint (g_object_set_data). */
#define RK_PRINT_UID_KEY "ridgekey-uid"

gboolean   store_init (const char *root, GError **error);

/* Prints of one user, or of every user (each tagged with RK_PRINT_UID_KEY). Never NULL. */
GPtrArray *store_load_user (uid_t uid);
GPtrArray *store_load_all (void);

/* One print, or NULL with error (G_FILE_ERROR_NOENT when there is none). */
FpPrint   *store_load (uid_t uid, FpFinger finger, GError **error);

gboolean   store_save (uid_t uid, FpPrint *print, GError **error);
gboolean   store_remove (uid_t uid, FpFinger finger, GError **error);

/* Fingers recorded for a user (array of FpFinger as guint). Never NULL. */
GArray    *store_list_fingers (uid_t uid);

#endif /* RIDGEKEY_STORE_H */
