#ifndef PM3DS_UPDATE_H
#define PM3DS_UPDATE_H
/* "A new version is out" notice (platform/pm3ds_update.c). */
#include <stdbool.h>

#define PM3DS_VERSION_CODE(a, b, c) ((a) * 10000 + (b) * 100 + (c))

void pm3ds_update_check_start(void);   /* once at boot; does nothing if Options turned it off */
int pm3ds_update_available(void);      /* the newer release's version code, or 0 */
void pm3ds_update_dismiss(bool skip);  /* skip: not again for this version (saved) */

#endif
