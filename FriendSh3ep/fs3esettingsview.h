#ifndef FS3ESETTINGSVIEW_H
#define FS3ESETTINGSVIEW_H

/*
 * fs3esettingsview.h - General Settings window for FriendSh3ep.
 *
 * Unlike fs3ethemeview.c (fonts/theme), this window covers settings that
 * are not related to rendering: cache/data directories, cache size, the
 * "flush cache" action, and the server polling interval.
 *
 * Layout:
 *   Paths group (vertical):
 *     Cache directory:     [drawer path........]
 *     User data directory: [drawer path........]
 *   Cache group (vertical):
 *     Max cache size (MB): [integer]
 *     [Flush cache]
 *   Server group (vertical):
 *     Check interval (seconds): [integer]
 *
 * Uses getfile.gadget (GETFILE_DrawersOnly) for directories and
 * integer.gadget for numeric fields -- same libraries fs3ethemeview.c and
 * EmojiGear/egsettingsview.c already open (GetFileBase / IntegerBase).
 *
 * Follows the same live-apply, save-on-close convention as fs3ethemeview.c:
 * no OK/Cancel/Apply buttons, every gadget writes into app->settings as
 * soon as it changes, and FS3ESettings_Save() runs on window close.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

typedef struct FS3ESettingsView {
    Object        *windowObj;
    struct Window *window;

    LONG left, top, width, height;

    Object *mainLayout;

    /* Paths group */
    Object *cachePathGF;
    Object *userDataPathGF;

    /* Cache group */
    Object *maxCacheSizeInt;
    Object *flushCacheBtn;

    /* Server group */
    Object *checkIntervalInt;

} FS3ESettingsView;

BOOL  FS3ESettingsView_Create(FS3ESettingsView *sv, const char *title);
void  FS3ESettingsView_Dispose(FS3ESettingsView *sv);
void  FS3ESettingsView_Open(FS3ESettingsView *sv);
void  FS3ESettingsView_Close(FS3ESettingsView *sv);
BOOL  FS3ESettingsView_HandleInput(FS3ESettingsView *sv);
ULONG FS3ESettingsView_GetSignalMask(FS3ESettingsView *sv);

#endif /* FS3ESETTINGSVIEW_H */
