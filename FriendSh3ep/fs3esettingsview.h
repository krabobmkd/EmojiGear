#ifndef FS3ESETTINGSVIEW_H
#define FS3ESETTINGSVIEW_H

/*
 * fs3esettingsview.h - General Settings window for FriendSh3ep.
 *
 * Unlike fs3ethemeview.c (fonts/theme), this window covers settings that
 * are not related to rendering: cache/data directories, cache size, the
 * "flush cache" action, and the server polling interval.
 *
 * The window's mainLayout holds a single child: a clicktab.gadget bar
 * (tabGadget) whose CLICKTAB_PageGroup is a page.gadget (pageGroup) with
 * one page per tab -- switching tabs is handled entirely by clicktab.gadget
 * itself (it drives PAGE_Current on the embedded page group), no explicit
 * WMHI_GADGETUP handling needed for GID_SETTINGSV_TABS. Each existing
 * BVS_GROUP box keeps its own identity/label; tabs just decide which ones
 * are stacked together on a given page:
 *
 *   Tab "User experience":
 *     Toot Timeline Playback group (was just "Playback"):
 *       Play toot time (seconds): [integer, 3..60]
 *       [x] Smooth auto scroll
 *     URL Link group:
 *       URL Link Action: [combo: Ask / Use OpenURL / Copy to Clipboard]
 *       [x] Direct download .zip,.lha
 *       Download directory: [drawer path........]
 *     Server Check group (was just "Server"):
 *       Check interval (seconds): [integer]
 *   Tab "Paths & Cache":
 *     Paths group (vertical):
 *       Cache directory:     [drawer path........]
 *       User data directory: [drawer path........]
 *     Cache group (vertical):
 *       Max cache size (MB): [integer]
 *       [x] Keep big user icons
 *       [x] Keep big thumbnails
 *       [Flush cache]
 *   Tab "Thumbnails & icons" (its sole page IS this group, same label):
 *     [x] Bigger Thumbnails
 *     [x] Minify thumbnails
 *     Scaling Quality:   [combo: Fast linear (68020) / Quick Bilinear (68030) / Full Trilinear (>=68060)]
 *     RGB Draw function: [combo: ScalePixelArray() / Internal Bilinear (>=68060)]
 *
 * Uses getfile.gadget (GETFILE_DrawersOnly) for directories and
 * integer.gadget for numeric fields -- same libraries fs3ethemeview.c and
 * EmojiGear/egsettingsview.c already open (GetFileBase / IntegerBase).
 * clicktab.gadget (ClickTabBase) is opened the same required-library way in
 * friendsh3ep.c's libraryTable -- no fallback class, unlike EmojiGear's
 * egtabs_fallbackclass.c, since every other gadget this window already
 * uses (chooser/integer/getfile/checkbox) is an unconditional ReAction
 * dependency too.
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

/* clicktab.gadget tabs, in display order -- see FS3ESettingsView_Create in
 * fs3esettingsview.c for what each page groups together. */
#define FS3ESETTINGSV_TAB_USEREXP    0
#define FS3ESETTINGSV_TAB_PATHSCACHE 1
#define FS3ESETTINGSV_TAB_THUMBS     2
#define FS3ESETTINGSV_TAB_COUNT      3

typedef struct FS3ESettingsView {
    Object        *windowObj;
    struct Window *window;

    LONG left, top, width, height;

    Object *mainLayout;

    /* Tab bar -- see this header's top comment. tabLabelNodes[] mirrors
     * scalingQualityNodes[]/rgbDrawFunctionNodes[] below: AllocClickTabNode'd,
     * not automatically freed by the gadget, so FS3ESettingsView_Dispose()
     * must FreeClickTabNode() them itself. */
    Object      *tabGadget;
    Object      *pageGroup;
    struct List  tabLabelsList;
    struct Node *tabLabelNodes[FS3ESETTINGSV_TAB_COUNT];

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
    Object *minifyThumbnailsCheck;
    Object *scalingQualityChooser;
    Object *rgbDrawFunctionChooser;
    struct List  scalingQualityList;
    struct Node *scalingQualityNodes[FS3E_SCALEQ_COUNT];
    struct List  rgbDrawFunctionList;
    struct Node *rgbDrawFunctionNodes[FS3E_RGBDRAW_COUNT];

    /* Playback group */
    Object *playTootTimeInt;
    Object *smoothAutoScrollCheck;

    /* URL Link group */
    Object *urlLinkActionChooser;
    struct List  urlLinkActionList;
    struct Node *urlLinkActionNodes[FS3E_URLLINK_COUNT];
    Object *directDownloadArchivesCheck;
    Object *downloadPathGF;
    Object *tootActionsDblClickCheck;

} FS3ESettingsView;

BOOL  FS3ESettingsView_Create(FS3ESettingsView *sv, const char *title);
void  FS3ESettingsView_Dispose(FS3ESettingsView *sv);
void  FS3ESettingsView_Open(FS3ESettingsView *sv);
void  FS3ESettingsView_Close(FS3ESettingsView *sv);
BOOL  FS3ESettingsView_HandleInput(FS3ESettingsView *sv);
ULONG FS3ESettingsView_GetSignalMask(FS3ESettingsView *sv);

#endif /* FS3ESETTINGSVIEW_H */
