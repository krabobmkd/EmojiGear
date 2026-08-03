/*
 * fs3esettingsview.c - General Settings window for FriendSh3ep.
 *
 * See fs3esettingsview.h. Pattern adapted from fs3ethemeview.c (window
 * shape, getfile gadgets) and EmojiGear/egsettingsview.c (integer gadget).
 */

#include <stdio.h>
#include <string.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/getfile.h>
#include <gadgets/getfile.h>

#include <proto/integer.h>
#include <gadgets/integer.h>

#include <proto/checkbox.h>
#include <gadgets/checkbox.h>

#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include <proto/clicktab.h>
#include <gadgets/clicktab.h>

#include <dos/dos.h>

#include "friendsh3ep.h"
#include "fs3esettingsview.h"
#include "fs3esettings.h"
#include "fs3elocale.h"
#include "fs3egadgetid.h"
#include "fs3eboopsimainwindow.h"
#include "network_fs3e/fs3enet.h"
#include "TootTimeline/fs3etoottimeline.h"

extern struct Library *GetFileBase;
extern struct Library *IntegerBase;
extern struct Library *ChooserBase;
extern struct Library *ClickTabBase;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static char *SettingsStrDup(const char *s)
{
    ULONG len;
    char *copy;
    if (!s) return NULL;
    len  = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) strcpy(copy, s);
    return copy;
}

/* Directory-only getfile gadget: no pattern gadget, no file selection --
 * the picked directory comes back through GETFILE_Drawer. */
static Object *makeDirGadget(ULONG gadId, const char *initialPath)
{
    return NewObject(GETFILE_GetClass(), NULL,
        GA_ID,               gadId,
        GA_RelVerify,        TRUE,
        GETFILE_TitleText,   (ULONG)LOC(MSG_SETTINGSV_PATHS_GROUP),
        GETFILE_RejectIcons, TRUE,
        GETFILE_DrawersOnly, TRUE,
        GETFILE_ReadOnly,    FALSE,
        GETFILE_Drawer,      (ULONG)(initialPath ? initialPath : ""),
        TAG_END);
}

/* Reads the picked directory back and stores a copy in *dest (full path,
 * unlike fs3ethemeview.c's font pickers which keep only the basename). */
static void updateDirPath(Object *gf, char **dest)
{
    ULONG strPtr = 0;
    GetAttr(GETFILE_Drawer, gf, &strPtr);
    if (strPtr) {
        FreeVec(*dest);
        *dest = SettingsStrDup((const char *)strPtr);
    }
}

static const ULONG scalingQualityMsgIds[FS3E_SCALEQ_COUNT] = {
    MSG_SETTINGSV_SCALEQ_FAST,
    MSG_SETTINGSV_SCALEQ_BILINEAR,
    MSG_SETTINGSV_SCALEQ_TRILINEAR,
};

static const ULONG rgbDrawFunctionMsgIds[FS3E_RGBDRAW_COUNT] = {
    MSG_SETTINGSV_RGBDRAW_SCALEPIXELARRAY,
    MSG_SETTINGSV_RGBDRAW_INTERNAL_BILINEAR,
};

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

BOOL FS3ESettingsView_Create(FS3ESettingsView *sv, const char *title)
{
    Object *pathsGroup;
    Object *cacheGroup;
    Object *serverGroup;
    Object *thumbnailsGroup;
    Object *playbackGroup;
    Object *urlLinkGroup;
    Object *userExpPage;
    Object *pathsAndCachePage;
    Object *thumbnailsSpacer;
    Object *pathsCacheSpacer;
    Object *cachePathLabel;
    Object *userDataPathLabel;
    Object *maxCacheSizeLabel;
    Object *checkIntervalLabel;
    Object *keepBigUserIconsLabel;
    Object *keepBigThumbnailsLabel;
    Object *biggerThumbnailsLabel;
    Object *minifyThumbnailsLabel;
    Object *scalingQualityLabel;
    Object *rgbDrawFunctionLabel;
    Object *playTootTimeLabel;
    Object *allowNextTootScrollLabel;
    Object *urlLinkActionLabel;
    Object *directDownloadArchivesLabel;
    Object *downloadPathLabel;
    Object *tootActionsDblClickLabel;
    ULONG   i;

    {
        LONG sl = sv->left, st = sv->top, sw = sv->width, sh = sv->height;
        memset(sv, 0, sizeof(*sv));
        sv->left = sl; sv->top = st; sv->width = sw; sv->height = sh;
    }

    /* --- Paths group --- */
    sv->cachePathGF    = makeDirGadget(GID_SETTINGSV_CACHE_PATH,    app->settings.cachePath);
    sv->userDataPathGF = makeDirGadget(GID_SETTINGSV_USERDATA_PATH, app->settings.userDataPath);
    if (!sv->cachePathGF || !sv->userDataPathGF) return FALSE;

    cachePathLabel    = NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_CACHE_PATH),    TAG_END);
    userDataPathLabel = NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_USERDATA_PATH), TAG_END);

    pathsGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_SETTINGSV_PATHS_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->cachePathGF,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)cachePathLabel,
        LAYOUT_AddChild,      (ULONG)sv->userDataPathGF,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)userDataPathLabel,
        TAG_END);
    if (!pathsGroup) return FALSE;

    /* --- Cache group --- */
    sv->maxCacheSizeInt = NewObject(INTEGER_GetClass(), NULL,
        GA_ID,           GID_SETTINGSV_MAX_CACHE_SIZE,
        GA_RelVerify,    TRUE,
        INTEGER_Number,  (ULONG)app->settings.maxCacheSizeMB,
        INTEGER_Minimum, 20L,
        INTEGER_Maximum, 120L,
        INTEGER_Arrows,  TRUE,
        TAG_END);
    if (!sv->maxCacheSizeInt) return FALSE;

    maxCacheSizeLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_MAX_CACHE_SIZE), TAG_END);

    sv->keepBigUserIconsCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_KEEP_BIG_USERICONS,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.keepBigUserIcons,
        TAG_END);
    if (!sv->keepBigUserIconsCheck) return FALSE;

    keepBigUserIconsLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_KEEP_BIG_USERICONS), TAG_END);

    sv->keepBigThumbnailsCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_KEEP_BIG_THUMBNAILS,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.keepBigThumbnails,
        TAG_END);
    if (!sv->keepBigThumbnailsCheck) return FALSE;

    keepBigThumbnailsLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_KEEP_BIG_THUMBNAILS), TAG_END);

    sv->flushCacheBtn = NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_FLUSH_CACHE,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)LOC(MSG_SETTINGSV_FLUSH_CACHE),
        TAG_END);
    if (!sv->flushCacheBtn) return FALSE;

    cacheGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_SETTINGSV_CACHE_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->maxCacheSizeInt,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)maxCacheSizeLabel,
        LAYOUT_AddChild,      (ULONG)sv->keepBigUserIconsCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)keepBigUserIconsLabel,
        LAYOUT_AddChild,      (ULONG)sv->keepBigThumbnailsCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)keepBigThumbnailsLabel,
        LAYOUT_AddChild,      (ULONG)sv->flushCacheBtn,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (!cacheGroup) return FALSE;

    /* --- Server group --- */
    sv->checkIntervalInt = NewObject(INTEGER_GetClass(), NULL,
        GA_ID,           GID_SETTINGSV_CHECK_INTERVAL,
        GA_RelVerify,    TRUE,
        INTEGER_Number,  (ULONG)app->settings.tootCheckIntervalSec,
        INTEGER_Minimum, 5L,
        INTEGER_Maximum, 3600L,
        INTEGER_Arrows,  TRUE,
        TAG_END);
    if (!sv->checkIntervalInt) return FALSE;

    checkIntervalLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_CHECK_INTERVAL), TAG_END);

    serverGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_SETTINGSV_SERVERCHECK_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->checkIntervalInt,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)checkIntervalLabel,
        TAG_END);
    if (!serverGroup) return FALSE;

    /* --- URL Link group --- */
    NewList(&sv->urlLinkActionList);
    {
        static const ULONG urlLinkActionMsgIds[FS3E_URLLINK_COUNT] = {
            MSG_SETTINGSV_URLLINK_ASK,
            MSG_SETTINGSV_URLLINK_OPENURL,
            MSG_SETTINGSV_URLLINK_CLIPBOARD,
        };
        for (i = 0; i < FS3E_URLLINK_COUNT; i++) {
            struct Node *node = NULL;
            if (ChooserBase)
                node = AllocChooserNode(CNA_Text, (ULONG)LOC(urlLinkActionMsgIds[i]), TAG_END);
            sv->urlLinkActionNodes[i] = node;
            if (node) AddTail(&sv->urlLinkActionList, node);
        }
    }

    sv->urlLinkActionChooser = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          GID_SETTINGSV_URLLINK_ACTION,
        GA_RelVerify,   TRUE,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&sv->urlLinkActionList,
        CHOOSER_Active, (ULONG)app->settings.urlLinkAction,
        TAG_END);
    if (!sv->urlLinkActionChooser) return FALSE;

    urlLinkActionLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_URLLINK_ACTION), TAG_END);

    sv->directDownloadArchivesCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_DIRECT_DL_ARCHIVES,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.directDownloadArchives,
        TAG_END);
    if (!sv->directDownloadArchivesCheck) return FALSE;

    directDownloadArchivesLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_DIRECT_DL_ARCHIVES), TAG_END);

    sv->downloadPathGF = makeDirGadget(GID_SETTINGSV_DOWNLOAD_PATH, app->settings.downloadPath);
    if (!sv->downloadPathGF) return FALSE;

    downloadPathLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_DOWNLOAD_PATH), TAG_END);

    sv->tootActionsDblClickCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_TOOT_ACTIONS_DBLCLICK,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.tootActionsNeedDoubleClick,
        TAG_END);
    if (!sv->tootActionsDblClickCheck) return FALSE;

    tootActionsDblClickLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_TOOT_ACTIONS_DBLCLICK), TAG_END);

    urlLinkGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_SETTINGSV_URLLINK_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->urlLinkActionChooser,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)urlLinkActionLabel,
        LAYOUT_AddChild,      (ULONG)sv->directDownloadArchivesCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)directDownloadArchivesLabel,
        LAYOUT_AddChild,      (ULONG)sv->downloadPathGF,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)downloadPathLabel,
        LAYOUT_AddChild,      (ULONG)sv->tootActionsDblClickCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)tootActionsDblClickLabel,
        TAG_END);
    if (!urlLinkGroup) return FALSE;

    /* --- Thumbnails & icons group --- */
    sv->biggerThumbnailsCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_BIGGER_THUMBNAILS,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.biggerThumbnails,
        TAG_END);
    if (!sv->biggerThumbnailsCheck) return FALSE;

    biggerThumbnailsLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_BIGGER_THUMBNAILS), TAG_END);

    sv->minifyThumbnailsCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_MINIFY_THUMBNAILS,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.minifyThumbnails,
        TAG_END);
    if (!sv->minifyThumbnailsCheck) return FALSE;

    minifyThumbnailsLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_MINIFY_THUMBNAILS), TAG_END);

    NewList(&sv->scalingQualityList);
    for (i = 0; i < FS3E_SCALEQ_COUNT; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)LOC(scalingQualityMsgIds[i]), TAG_END);
        sv->scalingQualityNodes[i] = node;
        if (node) AddTail(&sv->scalingQualityList, node);
    }

    sv->scalingQualityChooser = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          GID_SETTINGSV_SCALING_QUALITY,
        GA_RelVerify,   TRUE,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&sv->scalingQualityList,
        CHOOSER_Active, (ULONG)app->settings.scalingQuality,
        TAG_END);
    if (!sv->scalingQualityChooser) return FALSE;

    scalingQualityLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_SCALING_QUALITY), TAG_END);

    NewList(&sv->rgbDrawFunctionList);
    for (i = 0; i < FS3E_RGBDRAW_COUNT; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)LOC(rgbDrawFunctionMsgIds[i]), TAG_END);
        sv->rgbDrawFunctionNodes[i] = node;
        if (node) AddTail(&sv->rgbDrawFunctionList, node);
    }

    sv->rgbDrawFunctionChooser = NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          GID_SETTINGSV_RGB_DRAW_FUNCTION,
        GA_RelVerify,   TRUE,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&sv->rgbDrawFunctionList,
        CHOOSER_Active, (ULONG)app->settings.rgbDrawFunction,
        TAG_END);
    if (!sv->rgbDrawFunctionChooser) return FALSE;

    rgbDrawFunctionLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_RGB_DRAW_FUNCTION), TAG_END);

    /* Trailing filler -- an unadorned layout.gadget used purely as flexible
     * "glue" -- absorbs whatever vertical space the tab page has beyond
     * this group's natural content height, instead of the group's own
     * BVS_GROUP border stretching to fill it (see this file's header
     * comment: thumbnailsGroup IS the whole "Thumbnails & icons" tab page
     * now, no longer just one of several stacked groups). */
    thumbnailsSpacer = NewObject(LAYOUT_GetClass(), NULL, TAG_END);
    if (!thumbnailsSpacer) return FALSE;

    thumbnailsGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_SETTINGSV_THUMBNAILS_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->biggerThumbnailsCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)biggerThumbnailsLabel,
        LAYOUT_AddChild,      (ULONG)sv->minifyThumbnailsCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)minifyThumbnailsLabel,
        LAYOUT_AddChild,      (ULONG)sv->scalingQualityChooser,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)scalingQualityLabel,
        LAYOUT_AddChild,      (ULONG)sv->rgbDrawFunctionChooser,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)rgbDrawFunctionLabel,
        LAYOUT_AddChild,      (ULONG)thumbnailsSpacer,
        CHILD_WeightedHeight, 1,
        TAG_END);
    if (!thumbnailsGroup) return FALSE;

    /* --- Playback group --- */
    sv->playTootTimeInt = NewObject(INTEGER_GetClass(), NULL,
        GA_ID,           GID_SETTINGSV_PLAY_TOOT_TIME,
        GA_RelVerify,    TRUE,
        INTEGER_Number,  (ULONG)app->settings.playTootTimeSec,
        INTEGER_Minimum, 3L,
        INTEGER_Maximum, 60L,
        INTEGER_Arrows,  TRUE,
        TAG_END);
    if (!sv->playTootTimeInt) return FALSE;

    playTootTimeLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_PLAY_TOOT_TIME), TAG_END);

    sv->allowNextTootScrollCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_ALLOW_NEXT_TOOT_SCROLL,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.allowNextTootScroll,
        TAG_END);
    if (!sv->allowNextTootScrollCheck) return FALSE;

    allowNextTootScrollLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_ALLOW_NEXT_TOOT_SCROLL), TAG_END);

    playbackGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_SETTINGSV_TOOTPLAYBACK_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->playTootTimeInt,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)playTootTimeLabel,
        LAYOUT_AddChild,      (ULONG)sv->allowNextTootScrollCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)allowNextTootScrollLabel,
        TAG_END);
    if (!playbackGroup) return FALSE;

    /* --- "User experience" tab page: Toot Timeline Playback, URL Link,
     * Server Check, stacked vertically -- see this file's header comment. */
    userExpPage = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)playbackGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)urlLinkGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)serverGroup,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (!userExpPage) return FALSE;

    /* --- "Paths & Cache" tab page: Paths, Cache, stacked vertically, plus
     * a trailing spacer -- same "don't stretch the last group's border"
     * reasoning as thumbnailsSpacer above. --- */
    pathsCacheSpacer = NewObject(LAYOUT_GetClass(), NULL, TAG_END);
    if (!pathsCacheSpacer) return FALSE;

    pathsAndCachePage = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)pathsGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)cacheGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)pathsCacheSpacer,
        CHILD_WeightedHeight, 1,
        TAG_END);
    if (!pathsAndCachePage) return FALSE;

    /* --- "Thumbnails & icons" tab page: thumbnailsGroup itself IS the
     * page -- already a single BVS_GROUP box with that exact label, no
     * extra wrapper needed (see this file's header comment). --- */

    /* --- Tab bar: one clicktab node per page, in the same order they're
     * PAGE_Add'd below (TNA_Number must match that index -- see
     * clicktab_gc.doc's page.gadget notes on keeping the two in sync). --- */
    {
        static const ULONG tabMsgIds[FS3ESETTINGSV_TAB_COUNT] = {
            MSG_SETTINGSV_TAB_USEREXP,
            MSG_SETTINGSV_TAB_PATHSCACHE,
            MSG_SETTINGSV_THUMBNAILS_GROUP, /* same text as that tab's sole group */
        };

        NewList(&sv->tabLabelsList);
        for (i = 0; i < FS3ESETTINGSV_TAB_COUNT; i++) {
            struct Node *node = NULL;
            if (ClickTabBase)
                node = AllocClickTabNode(TNA_Text, (ULONG)LOC(tabMsgIds[i]),
                                          TNA_Number, i, TAG_END);
            sv->tabLabelNodes[i] = node;
            if (node) AddTail(&sv->tabLabelsList, node);
        }
    }

    /* page.gadget: lives in layout.gadget's own library (LayoutBase,
     * already opened), PAGE_GetClass() needs no separate PageBase --
     * disposes userExpPage/pathsAndCachePage/thumbnailsGroup itself when
     * the window (and this page object with it) is torn down. */
    sv->pageGroup = NewObject(PAGE_GetClass(), NULL,
        PAGE_Add, (ULONG)userExpPage,
        PAGE_Add, (ULONG)pathsAndCachePage,
        PAGE_Add, (ULONG)thumbnailsGroup,
        TAG_END);
    if (!sv->pageGroup) return FALSE;

    sv->tabGadget = NewObject(CLICKTAB_GetClass(), NULL,
        GA_ID,              GID_SETTINGSV_TABS,
        GA_RelVerify,       TRUE,
        CLICKTAB_Labels,    (ULONG)&sv->tabLabelsList,
        CLICKTAB_Current,   0L,
        CLICKTAB_PageGroup, (ULONG)sv->pageGroup,
        TAG_END);
    if (!sv->tabGadget) return FALSE;

    /* --- Top-level (mother) layout: just the tab bar. --- */
    sv->mainLayout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->tabGadget,
        CHILD_WeightedHeight, 1,
        TAG_END);
    if (!sv->mainLayout) return FALSE;

    /* --- BOOPSI window object --- */
    sv->windowObj = NewObject(WINDOW_GetClass(), NULL,
        WA_Left,  140,
        WA_Top,   100,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIZEGADGET |
                  WFLG_SMART_REFRESH,
        WA_Title, (ULONG)title,
        WINDOW_ParentGroup, (ULONG)sv->mainLayout,
        TAG_END);
    if (!sv->windowObj) {
        DisposeObject(sv->mainLayout);
        sv->mainLayout = NULL;
        return FALSE;
    }

    return TRUE;
}

void FS3ESettingsView_Dispose(FS3ESettingsView *sv)
{
    ULONG i;

    if (!sv) return;

    if (sv->windowObj) {
        FS3ESettingsView_Close(sv);
        DisposeObject(sv->windowObj);
        sv->windowObj = NULL;
    }

    if (ChooserBase) {
        for (i = 0; i < FS3E_SCALEQ_COUNT; i++) {
            if (sv->scalingQualityNodes[i]) {
                FreeChooserNode(sv->scalingQualityNodes[i]);
                sv->scalingQualityNodes[i] = NULL;
            }
        }
        for (i = 0; i < FS3E_RGBDRAW_COUNT; i++) {
            if (sv->rgbDrawFunctionNodes[i]) {
                FreeChooserNode(sv->rgbDrawFunctionNodes[i]);
                sv->rgbDrawFunctionNodes[i] = NULL;
            }
        }
        for (i = 0; i < FS3E_URLLINK_COUNT; i++) {
            if (sv->urlLinkActionNodes[i]) {
                FreeChooserNode(sv->urlLinkActionNodes[i]);
                sv->urlLinkActionNodes[i] = NULL;
            }
        }
    }

    if (ClickTabBase) {
        for (i = 0; i < FS3ESETTINGSV_TAB_COUNT; i++) {
            if (sv->tabLabelNodes[i]) {
                FreeClickTabNode(sv->tabLabelNodes[i]);
                sv->tabLabelNodes[i] = NULL;
            }
        }
    }
}

void FS3ESettingsView_Open(FS3ESettingsView *sv)
{
    if (!sv || !sv->windowObj) return;

    if (sv->window) {
        WindowToFront(sv->window);
        ActivateWindow(sv->window);
        return;
    }

    if (CurrentMainScreen) {
        SetAttrs(sv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    if (sv->width > 0) {
        SetAttrs(sv->windowObj,
                 WA_Left,   (ULONG)sv->left,
                 WA_Top,    (ULONG)sv->top,
                 WA_Width,  (ULONG)sv->width,
                 WA_Height, (ULONG)sv->height,
                 TAG_END);
    }

    sv->window = (struct Window *)DoMethod(sv->windowObj, WM_OPEN, NULL);
}

void FS3ESettingsView_Close(FS3ESettingsView *sv)
{
    if (!sv || !sv->windowObj || !sv->window) return;

    GetAttr(WA_Left,   sv->windowObj, (ULONG *)&sv->left);
    GetAttr(WA_Top,    sv->windowObj, (ULONG *)&sv->top);
    GetAttr(WA_Width,  sv->windowObj, (ULONG *)&sv->width);
    GetAttr(WA_Height, sv->windowObj, (ULONG *)&sv->height);

    FS3ESettings_Save(&app->settings);
    DoMethod(sv->windowObj, WM_CLOSE, NULL);
    sv->window = NULL;
}

BOOL FS3ESettingsView_HandleInput(FS3ESettingsView *sv)
{
    ULONG result;

    if (!sv || !sv->windowObj) return FALSE;
    if (!sv->window) return TRUE;

    while ((result = DoMethod(sv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3ESettingsView_Close(sv);
                return TRUE;

            case WMHI_RAWKEY:
            {
                ULONG key = result & 0x07f;
                ULONG isUp = (result & 0x080);

                if (key == 0x45 && isUp) { /* Esc */
                    FS3ESettingsView_Close(sv);
                    return TRUE;
                }
            }
                break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;

                if (gadId == GID_SETTINGSV_CACHE_PATH) {
                    if (gfRequestDir(sv->cachePathGF, sv->window))
                        updateDirPath(sv->cachePathGF, &app->settings.cachePath);

                } else if (gadId == GID_SETTINGSV_USERDATA_PATH) {
                    if (gfRequestDir(sv->userDataPathGF, sv->window))
                        updateDirPath(sv->userDataPathGF, &app->settings.userDataPath);

                } else if (gadId == GID_SETTINGSV_MAX_CACHE_SIZE) {
                    ULONG val = 0;
                    GetAttr(INTEGER_Number, sv->maxCacheSizeInt, &val);
                    if ((LONG)val < 1)    val = 1;
                    if ((LONG)val > 4096) val = 4096;
                    app->settings.maxCacheSizeMB = (int)val;

                } else if (gadId == GID_SETTINGSV_CHECK_INTERVAL) {
                    ULONG val = 0;
                    GetAttr(INTEGER_Number, sv->checkIntervalInt, &val);
                    if ((LONG)val < 5)    val = 5;
                    if ((LONG)val > 3600) val = 3600;
                    app->settings.tootCheckIntervalSec = (int)val;

                } else if (gadId == GID_SETTINGSV_PLAY_TOOT_TIME) {
                    ULONG val = 0;
                    GetAttr(INTEGER_Number, sv->playTootTimeInt, &val);
                    if ((LONG)val < 3)  val = 3;
                    if ((LONG)val > 60) val = 60;
                    app->settings.playTootTimeSec = (int)val;

                } else if (gadId == GID_SETTINGSV_ALLOW_NEXT_TOOT_SCROLL) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, sv->allowNextTootScrollCheck, &checked);
                    app->settings.allowNextTootScroll = checked ? TRUE : FALSE;

                } else if (gadId == GID_SETTINGSV_KEEP_BIG_USERICONS) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, sv->keepBigUserIconsCheck, &checked);
                    app->settings.keepBigUserIcons = checked ? TRUE : FALSE;

                } else if (gadId == GID_SETTINGSV_KEEP_BIG_THUMBNAILS) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, sv->keepBigThumbnailsCheck, &checked);
                    app->settings.keepBigThumbnails = checked ? TRUE : FALSE;

                } else if (gadId == GID_SETTINGSV_BIGGER_THUMBNAILS) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, sv->biggerThumbnailsCheck, &checked);
                    app->settings.biggerThumbnails = checked ? TRUE : FALSE;

                } else if (gadId == GID_SETTINGSV_MINIFY_THUMBNAILS) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, sv->minifyThumbnailsCheck, &checked);
                    app->settings.minifyThumbnails = checked ? TRUE : FALSE;

                } else if (gadId == GID_SETTINGSV_SCALING_QUALITY) {
                    ULONG active = 0;
                    GetAttr(CHOOSER_Active, sv->scalingQualityChooser, &active);
                    app->settings.scalingQuality = (int)active;

                } else if (gadId == GID_SETTINGSV_RGB_DRAW_FUNCTION) {
                    ULONG active = 0;
                    GetAttr(CHOOSER_Active, sv->rgbDrawFunctionChooser, &active);
                    app->settings.rgbDrawFunction = (int)active;

                } else if (gadId == GID_SETTINGSV_URLLINK_ACTION) {
                    ULONG active = 0;
                    GetAttr(CHOOSER_Active, sv->urlLinkActionChooser, &active);
                    app->settings.urlLinkAction = (int)active;

                } else if (gadId == GID_SETTINGSV_DIRECT_DL_ARCHIVES) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, sv->directDownloadArchivesCheck, &checked);
                    app->settings.directDownloadArchives = checked ? TRUE : FALSE;

                } else if (gadId == GID_SETTINGSV_DOWNLOAD_PATH) {
                    if (gfRequestDir(sv->downloadPathGF, sv->window))
                        updateDirPath(sv->downloadPathGF, &app->settings.downloadPath);

                } else if (gadId == GID_SETTINGSV_TOOT_ACTIONS_DBLCLICK) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, sv->tootActionsDblClickCheck, &checked);
                    app->settings.tootActionsNeedDoubleClick = checked ? TRUE : FALSE;
                    SetGdAttrs(app->tootTimeline,
                        TTIMELINE_ActionOnDoubleClick, (ULONG)app->settings.tootActionsNeedDoubleClick,
                        TAG_DONE);

                } else if (gadId == GID_SETTINGSV_FLUSH_CACHE) {
                    if (app->netRequestPort) {
                        struct MsgPort *replyPort = CreateMsgPort();
                        if (replyPort) {
                            FS3ENet_FlushCache(app->netRequestPort, replyPort);
                            DeleteMsgPort(replyPort);
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    }

    return TRUE;
}

ULONG FS3ESettingsView_GetSignalMask(FS3ESettingsView *sv)
{
    if (!sv || !sv->window) return 0;
    return (1L << sv->window->UserPort->mp_SigBit);
}
