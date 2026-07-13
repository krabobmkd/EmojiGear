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

#include <dos/dos.h>

#include "friendsh3ep.h"
#include "fs3esettingsview.h"
#include "fs3esettings.h"
#include "fs3elocale.h"
#include "fs3egadgetid.h"
#include "fs3eboopsimainwindow.h"
#include "network_fs3e/fs3enet.h"

extern struct Library *GetFileBase;
extern struct Library *IntegerBase;
extern struct Library *ChooserBase;

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
    Object *cachePathLabel;
    Object *userDataPathLabel;
    Object *maxCacheSizeLabel;
    Object *checkIntervalLabel;
    Object *keepBigUserIconsLabel;
    Object *keepBigThumbnailsLabel;
    Object *biggerThumbnailsLabel;
    Object *scalingQualityLabel;
    Object *rgbDrawFunctionLabel;
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
        INTEGER_Minimum, 1L,
        INTEGER_Maximum, 4096L,
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
        LAYOUT_Label,         (ULONG)LOC(MSG_SETTINGSV_SERVER_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)sv->checkIntervalInt,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)checkIntervalLabel,
        TAG_END);
    if (!serverGroup) return FALSE;

    /* --- Thumbnails & icons group --- */
    sv->biggerThumbnailsCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_SETTINGSV_BIGGER_THUMBNAILS,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.biggerThumbnails,
        TAG_END);
    if (!sv->biggerThumbnailsCheck) return FALSE;

    biggerThumbnailsLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_SETTINGSV_BIGGER_THUMBNAILS), TAG_END);

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
        LAYOUT_AddChild,      (ULONG)sv->scalingQualityChooser,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)scalingQualityLabel,
        LAYOUT_AddChild,      (ULONG)sv->rgbDrawFunctionChooser,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)rgbDrawFunctionLabel,
        TAG_END);
    if (!thumbnailsGroup) return FALSE;

    /* --- Top-level vertical layout --- */
    sv->mainLayout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)pathsGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)cacheGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)serverGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)thumbnailsGroup,
        CHILD_WeightedHeight, 0,
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
                if ((result & WMHI_KEYMASK) == 0x45) { /* Esc */
                    FS3ESettingsView_Close(sv);
                    return TRUE;
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

                } else if (gadId == GID_SETTINGSV_SCALING_QUALITY) {
                    ULONG active = 0;
                    GetAttr(CHOOSER_Active, sv->scalingQualityChooser, &active);
                    app->settings.scalingQuality = (int)active;

                } else if (gadId == GID_SETTINGSV_RGB_DRAW_FUNCTION) {
                    ULONG active = 0;
                    GetAttr(CHOOSER_Active, sv->rgbDrawFunctionChooser, &active);
                    app->settings.rgbDrawFunction = (int)active;

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
