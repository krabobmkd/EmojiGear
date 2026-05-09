/*
 * EgSettingsView.c - Settings window for Petmate Amiga.
 *
 * Screen mode group:
 *   [ ] Use Workbench screen mode          <- checkbox
 *   Screen Mode: [ 0xXXXXXXXX ] [Choose..]  <- display + ASL requester button
 *   Description: [ PAL: 320x256 High Res ] <- read-only mode name display
 *
 * "Choose..." opens an ASL_ScreenModeRequest requester.
 * Mode description is obtained via GetDisplayInfoData(DTAG_NAME).
 * When "Use Workbench mode" is checked, the mode controls are disabled.
 *
 * C89 compatible.
 */

#include <stdio.h>
#include <string.h>

#include <clib/alib_protos.h>

#include <intuition/screens.h>
#include <intuition/icclass.h>
#include <graphics/displayinfo.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/utility.h>
#include <proto/asl.h>
#include <libraries/asl.h>

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

#include <proto/palette.h>
#include <gadgets/palette.h>

#include <proto/integer.h>
#include <gadgets/integer.h>

#include "compilers.h"
#include "egsettingsview.h"
#include "emojigear.h"
#include "emojigearsettings.h"

#include <gadgets/unitexteditor.h>

#include "eglocale.h"
#include <stdio.h>
extern struct Screen *CurrentMainScreen;
extern struct Library *AslBase;
extern struct Library *PaletteBase;
extern struct Library *IntegerBase;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static ULONG penIndexToRRGGBB(struct Screen *scr, UWORD penIndex)
{
    ULONG rgb[3];
    ULONG r, g, b;
    GetRGB32(scr->ViewPort.ColorMap, (ULONG)penIndex, 1, rgb);
    r = rgb[0] >> 24;
    g = rgb[1] >> 24;
    b = rgb[2] >> 24;
    return (r << 16) | (g << 8) | b;
}

/* shared */
UWORD rrggbbToPenIndex(struct Screen *scr, ULONG rrggbb)
{
    ULONG r = (rrggbb >> 16) & 0xFF;
    ULONG g = (rrggbb >> 8)  & 0xFF;
    ULONG b =  rrggbb        & 0xFF;
    LONG found,maxpens;
    r = r * 0x01010101UL;
    g = g * 0x01010101UL;
    b = b * 0x01010101UL;
    maxpens = (LONG)(1 << scr->RastPort.BitMap->Depth);

    if(maxpens>256) maxpens = 256;
    /* hi people !! it's -1 lol */
    found = FindColor(scr->ViewPort.ColorMap, r, g, b,maxpens-1);

    return (found >= 0) ? (UWORD)found : 0;
}

static void syncPaletteToState(EgSettingsView *psv)
{
    ULONG numColors = 8;
    UWORD bgPen  = 0;
    UWORD penPen = 1;

    if (CurrentMainScreen) {
        numColors = (ULONG)(1 << CurrentMainScreen->RastPort.BitMap->Depth);
        bgPen  = rrggbbToPenIndex(CurrentMainScreen, app->appSettings.editorBgColor);
        penPen = rrggbbToPenIndex(CurrentMainScreen, app->appSettings.editorPenColor);
    }

    if (psv->bgColorPalette) {
        if (psv->window)
            SetGadgetAttrs((struct Gadget *)psv->bgColorPalette, psv->window, NULL,
                PALETTE_NumColours, numColors,
                PALETTE_Colour, (ULONG)bgPen,
                TAG_END);
        else
            SetAttrs(psv->bgColorPalette,
                PALETTE_NumColours, numColors,
                PALETTE_Colour, (ULONG)bgPen,
                TAG_END);
    }
    if (psv->penColorPalette) {
        if (psv->window)
            SetGadgetAttrs((struct Gadget *)psv->penColorPalette, psv->window, NULL,
                PALETTE_NumColours, numColors,
                PALETTE_Colour, (ULONG)penPen,
                TAG_END);
        else
            SetAttrs(psv->penColorPalette,
                PALETTE_NumColours, numColors,
                PALETTE_Colour, (ULONG)penPen,
                TAG_END);
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

BOOL EgSettingsView_Init(EgSettingsView *psv, const char *title)
{
    Object *bgColorLabel;
    Object *penColorLabel;
    Object *tabSpacesLabel;
    Object *visualizeTabsLabel;
    Object *tabsAreSpacesLabel;

    // Object *useOneColorBgLabel;
    // Object *bgImageLabel;
    // Object *bgImageRow;
    // Object *spacer;

    if (!psv) return FALSE;

    memset(psv, 0, sizeof(EgSettingsView));

    /* --- Editor display group --- */
    {
        ULONG numcolors = 16; // CurrentMainScreen
        psv->bgColorPalette = NewObject(PALETTE_GetClass(), NULL,
                                  GA_ID,           GAD_SETTINGS_EDITORBGCOLOR,
                                  GA_RelVerify,    TRUE,
                                  PALETTE_NumColours, numcolors,
                                  TAG_END);
        if (!psv->bgColorPalette) return FALSE;

        psv->penColorPalette = NewObject(PALETTE_GetClass(), NULL,
                                   GA_ID,           GAD_SETTINGS_EDITORPENCOLOR,
                                   GA_RelVerify,    TRUE,
                                   PALETTE_NumColours, numcolors,
                                   TAG_END);
        if (!psv->penColorPalette) return FALSE;
    }

    bgColorLabel = NewObject(LABEL_GetClass(), NULL,
                       LABEL_Text, (ULONG)LOC(MSG_SETTINGS_EDITORBGCOLOR),
                       TAG_END);

    penColorLabel = NewObject(LABEL_GetClass(), NULL,
                        LABEL_Text, (ULONG)LOC(MSG_SETTINGS_EDITORPENCOLOR),
                        TAG_END);

    psv->tabSpacesInteger = NewObject(INTEGER_GetClass(), NULL,
                               GA_ID,           GAD_SETTINGS_TABSPACES,
                               GA_RelVerify,    TRUE,
                               INTEGER_Number,  (ULONG)app->appSettings.tabSpaces,
                               INTEGER_Minimum, 2L,
                               INTEGER_Maximum, 12L,
                               INTEGER_Arrows,  TRUE,
                               TAG_END);
    if (!psv->tabSpacesInteger) return FALSE;

    tabSpacesLabel = NewObject(LABEL_GetClass(), NULL,
                         LABEL_Text, (ULONG)LOC(MSG_SETTINGS_TABSPACES),
                         TAG_END);

    psv->visualizeTabsCheck = NewObject(CHECKBOX_GetClass(), NULL,
                                  GA_ID,        GAD_SETTINGS_VISUALIZETABS,
                                  GA_RelVerify, TRUE,
                                  GA_Selected,  (ULONG)app->appSettings.visualizeTabs,
                                  TAG_END);
    if (!psv->visualizeTabsCheck) return FALSE;

    visualizeTabsLabel = NewObject(LABEL_GetClass(), NULL,
                             LABEL_Text, (ULONG)LOC(MSG_SETTINGS_VISUALIZETABS),
                             TAG_END);

    psv->tabsAreSpacesCheck = NewObject(CHECKBOX_GetClass(), NULL,
                                   GA_ID,        GAD_SETTINGS_TABSARESPACES,
                                   GA_RelVerify, TRUE,
                                   GA_Selected,  (ULONG)app->appSettings.tabsAreSpaces,
                                   TAG_END);
    if (!psv->tabsAreSpacesCheck) return FALSE;

    tabsAreSpacesLabel = NewObject(LABEL_GetClass(), NULL,
                              LABEL_Text, (ULONG)LOC(MSG_SETTINGS_TABSARESPACES),
                              TAG_END);

    psv->editorLayout = NewObject(LAYOUT_GetClass(), NULL,
                            LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                            LAYOUT_BevelStyle,  BVS_GROUP,
                            LAYOUT_Label,       (ULONG)LOC(MSG_SETTINGS_EDITORDISPLAY),
                            LAYOUT_BackFill,    NULL,
                            LAYOUT_SpaceOuter,  TRUE,
                            LAYOUT_SpaceInner,  TRUE,

                            LAYOUT_AddChild,     (ULONG)psv->bgColorPalette,
                            CHILD_WeightedHeight, 1,
                            CHILD_Label,          (ULONG)bgColorLabel,
                            CHILD_MinWidth, 256,
                            CHILD_MinHeight,48,
                            CHILD_MaxHeight,128,
                            LAYOUT_AddChild,     (ULONG)psv->penColorPalette,
                            CHILD_WeightedHeight, 1,
                            CHILD_Label,          (ULONG)penColorLabel,
                            CHILD_MinHeight,48,
                            CHILD_MaxHeight,128,

                            LAYOUT_AddChild,      (ULONG)psv->tabSpacesInteger,
                            CHILD_WeightedHeight, 0,
                            CHILD_Label,          (ULONG)tabSpacesLabel,

                            LAYOUT_AddChild,      (ULONG)psv->visualizeTabsCheck,
                            CHILD_WeightedHeight, 0,
                            CHILD_Label,          (ULONG)visualizeTabsLabel,

                            LAYOUT_AddChild,      (ULONG)psv->tabsAreSpacesCheck,
                            CHILD_WeightedHeight, 0,
                            CHILD_Label,          (ULONG)tabsAreSpacesLabel,

                            TAG_END);
    if (!psv->editorLayout) return FALSE;

    /* --- UI Background group --- */
    // psv->bgLayout = NewObject(LAYOUT_GetClass(), NULL,
    //                     LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
    //                     LAYOUT_BevelStyle,  BVS_GROUP,
    //                     LAYOUT_BackFill, NULL, /* remove optional background image so better read */
    //                     LAYOUT_Label,       (ULONG)LOC(MSG_SETTINGS_UI_BG_GROUP),
    //                     LAYOUT_SpaceOuter,  TRUE,
    //                     LAYOUT_SpaceInner,  TRUE,

    //                     LAYOUT_AddChild,     (ULONG)psv->useOneColorBgCheck,
    //                     CHILD_WeightedHeight, 0,
    //                     CHILD_Label,          (ULONG)useOneColorBgLabel,

    //                     LAYOUT_AddChild,     (ULONG)bgImageRow,
    //                     CHILD_WeightedHeight, 0,
    //                     CHILD_Label,          (ULONG)bgImageLabel,

    //                     TAG_END);
    // if (!psv->bgLayout) return FALSE;

//    spacer = (Object *)NewObject(BUTTON_GetClass(), NULL,
//        GA_ReadOnly, TRUE,
//        BUTTON_BevelStyle, BVS_NONE,
//        BUTTON_Transparent, TRUE,
//       // GA_Text,"",
//        TAG_END);


    /* --- Main top-level layout --- */
    psv->mainLayout = NewObject(LAYOUT_GetClass(), NULL,
                          LAYOUT_DeferLayout,   TRUE,
                          LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
                          LAYOUT_BevelStyle,    BVS_NONE,
                          LAYOUT_SpaceOuter,    TRUE,
                          LAYOUT_SpaceInner,    TRUE,

                          LAYOUT_AddChild,      (ULONG)psv->editorLayout,
                          CHILD_WeightedHeight, 0,

                          TAG_END);
    if (!psv->mainLayout) {
        DisposeObject(psv->editorLayout);
        psv->editorLayout      = NULL;
        psv->bgColorPalette    = NULL;
        psv->penColorPalette   = NULL;
        psv->tabSpacesInteger  = NULL;

        return FALSE;
    }

    /* --- BOOPSI window object --- */
    psv->windowObj = NewObject(WINDOW_GetClass(), NULL,
                         WA_Left,   100,
                         WA_Top,    60,
/* not specifying size make it ideal size from the layout.
                         WA_Width,  420,
                         WA_Height, 270,*/
                         WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
                         WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                                    WFLG_CLOSEGADGET | WFLG_ACTIVATE |
                                    WFLG_SMART_REFRESH,
                         WA_Title,  (ULONG)title,
                         WINDOW_ParentGroup, (ULONG)psv->mainLayout,
                         TAG_END);
    if (!psv->windowObj) {
        DisposeObject(psv->mainLayout);
        psv->mainLayout    = NULL;

        return FALSE;
    }

    return TRUE;
}

void EgSettingsView_Open(EgSettingsView *psv)
{
    if (!psv || !psv->windowObj) return;
    if (psv->window) return; /* already open */

    if (CurrentMainScreen) {
        SetAttrs(psv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    syncPaletteToState(psv);

    psv->window = (struct Window *)DoMethod(psv->windowObj, WM_OPEN, NULL);
}

void EgSettingsView_Close(EgSettingsView *psv)
{
    if (!psv || !psv->windowObj || !psv->window) return;

    DoMethod(psv->windowObj, WM_CLOSE, NULL);
    psv->window = NULL;
}

BOOL EgSettingsView_HandleInput(EgSettingsView *psv)
{
    ULONG result;

    if (!psv || !psv->windowObj) return FALSE;
    if (!psv->window) return TRUE; /* window closed, that's fine */

    while ((result = DoMethod(psv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                AppSettings_Save(&app->appSettings);
                EgSettingsView_Close(psv);
                return TRUE;
            case WMHI_RAWKEY:
                    {
                        /* keys managed at window level */
                        ULONG key = (result & WMHI_KEYMASK);
                        if(key == 0x45) {
                            AppSettings_Save(&app->appSettings);
                            EgSettingsView_Close(psv);
                            return TRUE;
                        }
                    }
                        break;
            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;
                if (gadId == GAD_SETTINGS_EDITORBGCOLOR) {
                    ULONG colorIdx = 0;
                    GetAttr(PALETTE_Colour, psv->bgColorPalette, &colorIdx);
                    if (CurrentMainScreen)
                        app->appSettings.editorBgColor =
                            penIndexToRRGGBB(CurrentMainScreen, (UWORD)colorIdx);

                    SetGdAttrs(app->textEditorObj,UTED_BgPen,colorIdx,TAG_END);

                } else if (gadId == GAD_SETTINGS_EDITORPENCOLOR) {
                    ULONG colorIdx = 0;
                    GetAttr(PALETTE_Colour, psv->penColorPalette, &colorIdx);
                    if (CurrentMainScreen)
                        app->appSettings.editorPenColor =
                            penIndexToRRGGBB(CurrentMainScreen, (UWORD)colorIdx);
                    SetGdAttrs(app->textEditorObj,UTED_TextPen,colorIdx,TAG_END);
                } else if (gadId == GAD_SETTINGS_TABSPACES) {
                    ULONG val = 0;
                    GetAttr(INTEGER_Number, psv->tabSpacesInteger, &val);
                    if ((LONG)val < 2)  val = 2;
                    if ((LONG)val > 12) val = 12;
                    app->appSettings.tabSpaces = (int)val;
                    SetGdAttrs(app->textEditorObj,UTED_TabSpaces,app->appSettings.tabSpaces,TAG_END);
                } else if (gadId == GAD_SETTINGS_VISUALIZETABS) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, psv->visualizeTabsCheck, &checked);
                    app->appSettings.visualizeTabs = checked ? TRUE : FALSE;
                    SetGdAttrs(app->textEditorObj, UTED_VisibleTabs,
                               (ULONG)app->appSettings.visualizeTabs, TAG_END);
                } else if (gadId == GAD_SETTINGS_TABSARESPACES) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, psv->tabsAreSpacesCheck, &checked);
                    app->appSettings.tabsAreSpaces = checked ? TRUE : FALSE;
                    SetGdAttrs(app->textEditorObj, UTED_TabsAreSpaces,
                               (ULONG)app->appSettings.tabsAreSpaces, TAG_END);
                }
                break;
            }

            default:
                break;
        }
    }

    return TRUE;
}

ULONG EgSettingsView_GetSignalMask(EgSettingsView *psv)
{
    if (!psv || !psv->window) return 0;
    return (1L << psv->window->UserPort->mp_SigBit);
}

void EgSettingsView_Dispose(EgSettingsView *psv)
{
    if (!psv) return;

    if (psv->window) {
        DoMethod(psv->windowObj, WM_CLOSE, NULL);
        psv->window = NULL;
    }

    if (psv->windowObj) {
        DisposeObject(psv->windowObj);
        psv->windowObj       = NULL;
        psv->mainLayout      = NULL;

        psv->editorLayout    = NULL;

        psv->bgColorPalette    = NULL;
        psv->penColorPalette   = NULL;
        psv->tabSpacesInteger   = NULL;
        psv->visualizeTabsCheck = NULL;
        psv->tabsAreSpacesCheck = NULL;
    }
}
