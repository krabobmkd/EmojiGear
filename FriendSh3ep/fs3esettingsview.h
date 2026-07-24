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
 *     [x] Keep big user icons
 *     [x] Keep big thumbnails
 *     [Flush cache]
 *   Server group (vertical):
 *     Check interval (seconds): [integer]
 *   Thumbnails & icons group (vertical):
 *     [x] Bigger Thumbnails
 *     Scaling Quality:   [combo: Fast linear (68020) / Quick Bilinear (68030) / Full Trilinear (>=68060)]
 *     RGB Draw function: [combo: ScalePixelArray() / Internal Bilinear (>=68060)]
 *   Playback group (vertical):
 *     Play toot time (seconds): [integer, 3..60]
 *     [x] Allow next toot scroll
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
#include <exec/lists.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>

#include "fs3esettings.h"

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
    Object *keepBigUserIconsCheck;
    Object *keepBigThumbnailsCheck;

    /* Server group */
    Object *checkIntervalInt;

    /* Thumbnails & icons group */
    Object *biggerThumbnailsCheck;
    Object *scalingQualityChooser;
    Object *rgbDrawFunctionChooser;
    struct List  scalingQualityList;
    struct Node *scalingQualityNodes[FS3E_SCALEQ_COUNT];
    struct List  rgbDrawFunctionList;
    struct Node *rgbDrawFunctionNodes[FS3E_RGBDRAW_COUNT];

    /* Playback group */
    Object *playTootTimeInt;
    Object *allowNextTootScrollCheck;

} FS3ESettingsView;

BOOL  FS3ESettingsView_Create(FS3ESettingsView *sv, const char *title);
void  FS3ESettingsView_Dispose(FS3ESettingsView *sv);
void  FS3ESettingsView_Open(FS3ESettingsView *sv);
void  FS3ESettingsView_Close(FS3ESettingsView *sv);
BOOL  FS3ESettingsView_HandleInput(FS3ESettingsView *sv);
ULONG FS3ESettingsView_GetSignalMask(FS3ESettingsView *sv);

#endif /* FS3ESETTINGSVIEW_H */
