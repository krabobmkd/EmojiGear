/*
 * EgFontsView.c - Font Settings window for EmojiGear.
 *
 * C89 compatible.
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

#include <proto/checkbox.h>
#include <gadgets/checkbox.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/getfile.h>
#include <gadgets/getfile.h>

#include <dos/dos.h>
#include <utility/hooks.h>
#include <libraries/asl.h>

#include "compilers.h"
#include "egfontsview.h"
#include "emojigear.h"
#include "emojigearsettings.h"
#include "eglocale.h"

extern struct Screen *CurrentMainScreen;
extern struct Library *GetFileBase;
extern void UpdateEditorFontsFromSettings();

/* ------------------------------------------------------------------ */
/* Extension filter hook: accept .ttf and .odt, always accept dirs    */
/* ------------------------------------------------------------------ */
/*
static ULONG fontFilterFunc(
    REG(a0, struct Hook *hook),
    REG(a2, struct FileRequester *req),
    REG(a1, struct FileInfoBlock *fib))
{
    const char *name;
    ULONG len;

    (void)hook;
    (void)req;

    if (fib->fib_DirEntryType > 0) return TRUE;

    name = (const char *)fib->fib_FileName;
    len  = (ULONG)strlen(name);

    if (len > 4 && strcasecmp(name + len - 4, ".ttf") == 0) return TRUE;
    if (len > 4 && strcasecmp(name + len - 4, ".odt") == 0) return TRUE;

    return FALSE;
}

static struct Hook s_fontFilterHook;
*/
/* ------------------------------------------------------------------ */
/* Helper: duplicate a string via AllocVec                            */
/* ------------------------------------------------------------------ */

static char *FontStrDup(const char *s)
{
    char *copy;
    ULONG len;
    if (!s) return NULL;
    len  = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) strcpy(copy, s);
    return copy;
}

/* ------------------------------------------------------------------ */
/* Helper: create one getfile gadget with filter hook                 */
/* ------------------------------------------------------------------ */

static Object *makeGetFileGadget(ULONG gadId, const char *initialPath)
{
    return NewObject(GETFILE_GetClass(), NULL,
        GA_ID,              gadId,
        GA_RelVerify,       TRUE,
        GETFILE_TitleText,  (ULONG)LOC(MSG_FONTSETTINGS_FONTS_GROUP),
      //  GETFILE_FilterFunc, (ULONG)&s_fontFilterHook,
        GETFILE_RejectIcons, TRUE,
        GETFILE_DoPatterns,TRUE,
        GETFILE_Drawer,(ULONG)"Fonts:",
        GETFILE_Pattern,(ULONG)"#?(ttf|odt)",
        GETFILE_FilterDrawers, TRUE,
        GETFILE_ReadOnly,   FALSE,
        GETFILE_FullFileExpand, FALSE,
        (initialPath && initialPath[0]) ?
            GETFILE_File : TAG_IGNORE, (ULONG)initialPath,
        TAG_END);
}

/* ------------------------------------------------------------------ */
/* Sync checkboxes to app->appSettings after a preset is applied      */
/* ------------------------------------------------------------------ */

static void syncCheckboxesToState(EgFontsView *pfv)
{
    if (!pfv->window) return;
    SetAttrs((struct Gadget *)pfv->antialiasCheck,
                   GA_Selected, (ULONG)app->appSettings.antialias?1:0,
                   TAG_END);
    SetAttrs((struct Gadget *)pfv->emojiQualityCheck,
                   GA_Selected, (ULONG)app->appSettings.emojiQuality?1:0,
                   TAG_END);
    RefreshGList((struct Gadget *)pfv->antialiasCheck, pfv->window, NULL, 1);
    RefreshGList((struct Gadget *)pfv->emojiQualityCheck, pfv->window, NULL, 1);
}
static void syncFontPaths(EgFontsView *pfv)
{
    if (!pfv->window) return;
    SetGadgetAttrs((struct Gadget *)pfv->primaryFontGF,
                   pfv->window, NULL,
                   GETFILE_File, (ULONG)app->appSettings.primaryFontPath,
                   TAG_END);
    SetGadgetAttrs((struct Gadget *)pfv->fallback1FontGF,
                   pfv->window, NULL,
                   GETFILE_File, (ULONG)app->appSettings.fallback1FontPath,
                   TAG_END);
    SetGadgetAttrs((struct Gadget *)pfv->fallback2FontGF,
                   pfv->window, NULL,
                   GETFILE_File, (ULONG)app->appSettings.fallback2FontPath,
                   TAG_END);
    SetGadgetAttrs((struct Gadget *)pfv->emojiFontGF,
                   pfv->window, NULL,
                   GETFILE_File, (ULONG)app->appSettings.emojiFontPath,
                   TAG_END);

}


static void removeAllFonts()
{
    FreeVec(app->appSettings.primaryFontPath);
    app->appSettings.primaryFontPath = NULL;
     FreeVec(app->appSettings.fallback1FontPath);
    app->appSettings.fallback1FontPath = NULL;
    FreeVec(app->appSettings.fallback2FontPath);
    app->appSettings.fallback2FontPath = NULL;
    FreeVec(app->appSettings.emojiFontPath);
    app->appSettings.emojiFontPath = NULL;

}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

BOOL EgFontsView_Init(EgFontsView *pfv, const char *title)
{
    Object *optionsGroup;
    Object *fontsGroup;
    Object *presetsGroup;

    Object *antialiasLabel;
    Object *emojiQualityLabel;
    Object *primaryLabel;
    Object *fallback1Label;
    Object *fallback2Label;
    Object *emojiFontLabel;

    if (!pfv) return FALSE;

    memset(pfv, 0, sizeof(EgFontsView));

    /* Init filter hook once */
  //  memset(&s_fontFilterHook, 0, sizeof(s_fontFilterHook));
  //  s_fontFilterHook.h_Entry = (ULONG(*)())fontFilterFunc;

    /* --- Checkboxes --- */
    pfv->antialiasCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GAD_FONTS_ANTIALIAS,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->appSettings.antialias,
        TAG_END);
    if (!pfv->antialiasCheck) return FALSE;

    antialiasLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_FONTSETTINGS_ANTIALIAS),
        TAG_END);

    pfv->emojiQualityCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GAD_FONTS_EMOJIQUALITY,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->appSettings.emojiQuality,
        TAG_END);
    if (!pfv->emojiQualityCheck) return FALSE;

    emojiQualityLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_FONTSETTINGS_EMOJIQUALITY),
        TAG_END);

    /* --- Options horizontal group --- */
    optionsGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,   BVS_GROUP,
        LAYOUT_Label,        (ULONG)LOC(MSG_FONTSETTINGS_OPTIONS_GROUP),
        LAYOUT_BackFill,     NULL,
        LAYOUT_SpaceOuter,   TRUE,
        LAYOUT_SpaceInner,   TRUE,

        LAYOUT_AddChild,      (ULONG)pfv->antialiasCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)antialiasLabel,

        LAYOUT_AddChild,      (ULONG)pfv->emojiQualityCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)emojiQualityLabel,

        TAG_END);
    if (!optionsGroup) return FALSE;

    /* --- Getfile gadgets --- */
    pfv->primaryFontGF = makeGetFileGadget(GAD_FONTS_PRIMARY_FONT,
                                           app->appSettings.primaryFontPath);
    if (!pfv->primaryFontGF) return FALSE;

    pfv->fallback1FontGF = makeGetFileGadget(GAD_FONTS_FALLBACK1_FONT,
                                             app->appSettings.fallback1FontPath);
    if (!pfv->fallback1FontGF) return FALSE;

    pfv->fallback2FontGF = makeGetFileGadget(GAD_FONTS_FALLBACK2_FONT,
                                             app->appSettings.fallback2FontPath);
    if (!pfv->fallback2FontGF) return FALSE;

    pfv->emojiFontGF = makeGetFileGadget(GAD_FONTS_EMOJI_FONT,
                                         app->appSettings.emojiFontPath);
    if (!pfv->emojiFontGF) return FALSE;

    primaryLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_FONTSETTINGS_PRIMARY),
        TAG_END);
    fallback1Label = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_FONTSETTINGS_FALLBACK1),
        TAG_END);
    fallback2Label = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_FONTSETTINGS_FALLBACK2),
        TAG_END);
    emojiFontLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_FONTSETTINGS_EMOJIFONT),
        TAG_END);

    /* --- [getfile][X] row sub-layouts (owned by fontsGroup) --- */
    {
        Object *primaryRow, *fallback1Row, *fallback2Row, *emojiFontRow;
        Object *primaryClearBtn, *fallback1ClearBtn, *fallback2ClearBtn, *emojyClearBtn;

        primaryClearBtn = NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        GAD_FONTS_PRIMARY_CLEAR,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"X",
            TAG_END);
        if (!primaryClearBtn) return FALSE;

        fallback1ClearBtn = NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        GAD_FONTS_FALLBACK1_CLEAR,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"X",
            TAG_END);
        if (!fallback1ClearBtn) return FALSE;

        fallback2ClearBtn = NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        GAD_FONTS_FALLBACK2_CLEAR,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"X",
            TAG_END);
        if (!fallback2ClearBtn) return FALSE;

        emojyClearBtn = NewObject(BUTTON_GetClass(), NULL,
            GA_ID,        GAD_FONTS_EMOJI_CLEAR,
            GA_RelVerify, TRUE,
            GA_Text,      (ULONG)"X",
            TAG_END);
        if (!emojyClearBtn) return FALSE;

        primaryRow = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,   BVS_NONE,
            LAYOUT_SpaceInner,   FALSE,
            LAYOUT_AddChild,     (ULONG)pfv->primaryFontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,     (ULONG)primaryClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!primaryRow) return FALSE;

        fallback1Row = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,   BVS_NONE,
            LAYOUT_SpaceInner,   FALSE,
            LAYOUT_AddChild,     (ULONG)pfv->fallback1FontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,     (ULONG)fallback1ClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!fallback1Row) return FALSE;

        fallback2Row = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,   BVS_NONE,
            LAYOUT_SpaceInner,   FALSE,
            LAYOUT_AddChild,     (ULONG)pfv->fallback2FontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,     (ULONG)fallback2ClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!fallback2Row) return FALSE;

        emojiFontRow = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,   BVS_NONE,
            LAYOUT_SpaceInner,   FALSE,
            LAYOUT_AddChild,     (ULONG)pfv->emojiFontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,     (ULONG)emojyClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!emojiFontRow) return FALSE;

        /* --- Fonts vertical group --- */
        fontsGroup = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle,   BVS_GROUP,
            LAYOUT_Label,        (ULONG)LOC(MSG_FONTSETTINGS_FONTS_GROUP),
            LAYOUT_BackFill,     NULL,
            LAYOUT_SpaceOuter,   TRUE,
            LAYOUT_SpaceInner,   TRUE,

            LAYOUT_AddChild,      (ULONG)primaryRow,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)primaryLabel,

            LAYOUT_AddChild,      (ULONG)fallback1Row,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)fallback1Label,

            LAYOUT_AddChild,      (ULONG)fallback2Row,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)fallback2Label,

            LAYOUT_AddChild,      (ULONG)emojiFontRow,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)emojiFontLabel,

            TAG_END);
        if (!fontsGroup) return FALSE;
    }

    /* --- Preset buttons --- */
    pfv->presetLowBtn = NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        GAD_FONTS_PRESET_LOW,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)LOC(MSG_FONTSETTINGS_PRESET_LOW),
        TAG_END);
    if (!pfv->presetLowBtn) return FALSE;

    pfv->presetHQBtn = NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        GAD_FONTS_PRESET_HQ,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)LOC(MSG_FONTSETTINGS_PRESET_HQ),
        TAG_END);
    if (!pfv->presetHQBtn) return FALSE;

    pfv->presetMonoBtn = NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        GAD_FONTS_PRESET_MONO,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)LOC(MSG_FONTSETTINGS_PRESET_MONO),
        TAG_END);
    if (!pfv->presetMonoBtn) return FALSE;

    /* --- Presets horizontal group --- */
    presetsGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,   BVS_GROUP,
        LAYOUT_Label,        (ULONG)LOC(MSG_FONTSETTINGS_PRESETS_GROUP),
        LAYOUT_BackFill,     NULL,
        LAYOUT_SpaceOuter,   TRUE,
        LAYOUT_SpaceInner,   TRUE,

        LAYOUT_AddChild,      (ULONG)pfv->presetLowBtn,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild,      (ULONG)pfv->presetHQBtn,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild,      (ULONG)pfv->presetMonoBtn,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,

        TAG_END);
    if (!presetsGroup) return FALSE;

    /* --- Top-level vertical layout --- */
    pfv->mainLayout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_DeferLayout,  TRUE,
        LAYOUT_Orientation,  LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,   BVS_NONE,
        LAYOUT_SpaceOuter,   TRUE,
        LAYOUT_SpaceInner,   TRUE,

        LAYOUT_AddChild,     (ULONG)optionsGroup,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild,     (ULONG)fontsGroup,
        CHILD_WeightedHeight, 0,

        LAYOUT_AddChild,     (ULONG)presetsGroup,
        CHILD_WeightedHeight, 0,

        TAG_END);
    if (!pfv->mainLayout) return FALSE;

    /* --- BOOPSI window object --- */
    pfv->windowObj = NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   120,
        WA_Top,    80,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                   WFLG_CLOSEGADGET | WFLG_ACTIVATE |
                   WFLG_SMART_REFRESH,
        WA_Title,  (ULONG)title,
        WINDOW_ParentGroup, (ULONG)pfv->mainLayout,
        TAG_END);
    if (!pfv->windowObj) {
        DisposeObject(pfv->mainLayout);
        pfv->mainLayout = NULL;
        return FALSE;
    }

    return TRUE;
}

void EgFontsView_Open(EgFontsView *pfv)
{
    if (!pfv || !pfv->windowObj) return;
    if (pfv->window) return;

    if (CurrentMainScreen) {
        SetAttrs(pfv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    pfv->window = (struct Window *)DoMethod(pfv->windowObj, WM_OPEN, NULL);
}

void EgFontsView_Close(EgFontsView *pfv)
{
    if (!pfv || !pfv->windowObj || !pfv->window) return;

    DoMethod(pfv->windowObj, WM_CLOSE, NULL);
    pfv->window = NULL;
}

/* Read GETFILE_FullFile from a gadget; update *dest in AppSettings. */
static void updateFontPath(Object *gf, char **dest)
{
    ULONG strPtr = 0;
    GetAttr(/*GETFILE_FullFile*/GETFILE_File, gf, &strPtr);

    if (strPtr) {
        const char *part = (const char *)FilePart((STRPTR)strPtr);
        FreeVec(*dest);
        *dest = FontStrDup(part);

        UpdateEditorFontsFromSettings();
    }
}

BOOL EgFontsView_HandleInput(EgFontsView *pfv)
{
    ULONG result;

    if (!pfv || !pfv->windowObj) return FALSE;
    if (!pfv->window) return TRUE;

    while ((result = DoMethod(pfv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                AppSettings_Save(&app->appSettings);
                EgFontsView_Close(pfv);
                return TRUE;

            case WMHI_RAWKEY:
                if ((result & WMHI_KEYMASK) == 0x45) { /* Esc */
                    AppSettings_Save(&app->appSettings);
                    EgFontsView_Close(pfv);
                    return TRUE;
                }
                break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;

                if (gadId == GAD_FONTS_ANTIALIAS) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, pfv->antialiasCheck, &checked);
                    app->appSettings.antialias = checked ? TRUE : FALSE;
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_EMOJIQUALITY) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, pfv->emojiQualityCheck, &checked);
                    app->appSettings.emojiQuality = checked ? TRUE : FALSE;
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_PRIMARY_FONT) {
                    if (gfRequestFile(pfv->primaryFontGF, pfv->window))                    
                        updateFontPath(pfv->primaryFontGF,
                                       &app->appSettings.primaryFontPath);

                } else if (gadId == GAD_FONTS_FALLBACK1_FONT) {
                    if (gfRequestFile(pfv->fallback1FontGF, pfv->window))
                        updateFontPath(pfv->fallback1FontGF,
                                       &app->appSettings.fallback1FontPath);

                } else if (gadId == GAD_FONTS_FALLBACK2_FONT) {
                    if (gfRequestFile(pfv->fallback2FontGF, pfv->window))
                        updateFontPath(pfv->fallback2FontGF,
                                       &app->appSettings.fallback2FontPath);

                } else if (gadId == GAD_FONTS_EMOJI_FONT) {
                    if (gfRequestFile(pfv->emojiFontGF, pfv->window))
                        updateFontPath(pfv->emojiFontGF,
                                       &app->appSettings.emojiFontPath);

                } else if (gadId == GAD_FONTS_PRIMARY_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)pfv->primaryFontGF,
                                   pfv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->appSettings.primaryFontPath);
                    app->appSettings.primaryFontPath = NULL;
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_FALLBACK1_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)pfv->fallback1FontGF,
                                   pfv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->appSettings.fallback1FontPath);
                    app->appSettings.fallback1FontPath = NULL;
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_FALLBACK2_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)pfv->fallback2FontGF,
                                   pfv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->appSettings.fallback2FontPath);
                    app->appSettings.fallback2FontPath = NULL;
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_EMOJI_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)pfv->emojiFontGF,
                                   pfv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->appSettings.emojiFontPath);
                    app->appSettings.emojiFontPath = NULL;
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_PRESET_LOW) {
                    app->appSettings.antialias    = FALSE;
                    app->appSettings.emojiQuality = FALSE;

                    syncCheckboxesToState(pfv);

                    removeAllFonts();
                    app->appSettings.primaryFontPath = FontStrDup("LiberationSans-Regular.ttf");
                    app->appSettings.emojiFontPath = FontStrDup("OpenMoji-black-glyf.ttf");
                    syncFontPaths(pfv);
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_PRESET_HQ) {
                    app->appSettings.antialias    = TRUE;
                    app->appSettings.emojiQuality = TRUE;
                    syncCheckboxesToState(pfv);

                    removeAllFonts();
                    app->appSettings.primaryFontPath = FontStrDup("LiberationSans-Regular.ttf");
                    app->appSettings.emojiFontPath = FontStrDup("NotoColorEmoji32.ttf");
                    syncFontPaths(pfv);
                    UpdateEditorFontsFromSettings();

                } else if (gadId == GAD_FONTS_PRESET_MONO) {
                    app->appSettings.antialias    = TRUE;
                    app->appSettings.emojiQuality = TRUE;
                    syncCheckboxesToState(pfv);

                    removeAllFonts();
                    app->appSettings.primaryFontPath = FontStrDup("UbuntuMono-Regular.ttf");
                    app->appSettings.emojiFontPath = FontStrDup("NotoColorEmoji32.ttf");
                    syncFontPaths(pfv);
                    UpdateEditorFontsFromSettings();
                }
                break;
            }

            default:
                break;
        }
    }

    return TRUE;
}

ULONG EgFontsView_GetSignalMask(EgFontsView *pfv)
{
    if (!pfv || !pfv->window) return 0;
    return (1L << pfv->window->UserPort->mp_SigBit);
}

void EgFontsView_Dispose(EgFontsView *pfv)
{
    if (!pfv) return;

    if (pfv->window) {
        DoMethod(pfv->windowObj, WM_CLOSE, NULL);
        pfv->window = NULL;
    }

    if (pfv->windowObj) {
        DisposeObject(pfv->windowObj);
        pfv->windowObj        = NULL;
        pfv->mainLayout       = NULL;
        pfv->antialiasCheck   = NULL;
        pfv->emojiQualityCheck = NULL;
        pfv->primaryFontGF    = NULL;
        pfv->fallback1FontGF  = NULL;
        pfv->fallback2FontGF  = NULL;
        pfv->emojiFontGF      = NULL;
        pfv->presetLowBtn     = NULL;
        pfv->presetHQBtn      = NULL;
        pfv->presetMonoBtn    = NULL;
    }
}
