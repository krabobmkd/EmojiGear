/*
 * egsearchbox.c - Search & Replace box layout for EmojiGear.
 */

#include <string.h>

#include <proto/intuition.h>
#include <proto/alib.h>

#include <proto/layout.h>
#include <gadgets/layout.h>
#include <proto/button.h>

#include <gadgets/button.h>
#include <proto/checkbox.h>
#include <proto/label.h>
#include <gadgets/checkbox.h>
#include <proto/window.h>
#include <classes/window.h>

#include "emojibox.h"

#include <proto/unitexteditor.h>
#include <gadgets/unitexteditor.h>

#include <intuition/icclass.h>

#include "eglocale.h"
#include "gadgetid.h"
#include "boopsimessage.h"
#include "boopsimainwindow.h"
#include "egsearchbox.h"
#include "emojigear.h"
#include "egaction.h"

BOOL EgSearchBox_Create(EgSearchBox *sb, ULONG pointSize/*,ULONG *sharedButtonsDrawContext*/)
{
    Object *searchLabel;
    Object *searchLine;
    Object *replaceLabel;
    Object *replaceLine;
    Object *buttonLine;
    ULONG buttonsDc;
    {
        LONG sl = sb->left, st = sb->top, sw = sb->width, sh = sb->height;
        memset(sb, 0, sizeof(*sb));
        sb->left = sl; sb->top = st; sb->width = sw; sb->height = sh;
    }

    /* ------------------------------------------------------------------ */
    /* Line 1: "Search :"  [UniTextEditor]  [X]                           */
    /* ------------------------------------------------------------------ */
    searchLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text,                (ULONG)LOC(MSG_SEARCH_LABEL),
        TAG_END);

    sb->searchEditor = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,                  (ULONG)ID_SEARCH_EDITOR,
        ICA_TARGET,             (ULONG)TargetInstance,
        UTED_KeyMessageMode,    UKM_External, /* send rawkey/vanilla keys messages from window */
        UTED_BevelStyle,        BVS_FIELD,
        UTED_PointSize,         pointSize,
        UTED_AddFont,           (ULONG)"LiberationSans-Regular.ttf",
        UTED_AddFont,           (ULONG)"NotoColorEmoji32.ttf",
        UTED_TextPen,           1UL,
        UTED_BgPen,             0UL,
        UTED_MaxDisplayLines,   1UL,
        UTED_NoLineFeed,        TRUE,
        UTED_WordWrap,          FALSE,
        UTED_LeftMargin,2,
        UTED_TopMargin,3,
        UTED_BottomMargin,1,
        UTED_LineSpacing,0,
        TAG_END);

    if(sb->searchEditor)
    {
        GetAttr(UTED_URPDrawContext,sb->searchEditor,(ULONG*)&buttonsDc);
    }


    sb->searchEraseBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   (ULONG)ID_SEARCH_ERASE,
        GA_Text, (ULONG)LOC(MSG_SEARCH_ERASE),
        GA_RelVerify,TRUE, /* needed to receive WMHI_GADGETUP */
        TAG_END);

    searchLine = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        // LAYOUT_AddChild,    (ULONG)searchLabel,
        //     CHILD_WeightedWidth, 0,
        //     CHILD_MinWidth,80,
        LAYOUT_AddChild,    (ULONG)sb->searchEditor,
            CHILD_WeightedWidth, 1,
            CHILD_Label,(ULONG)searchLabel,
        LAYOUT_AddChild,    (ULONG)sb->searchEraseBtn,
            CHILD_WeightedWidth, 0,
        TAG_END);

    /* ------------------------------------------------------------------ */
    /* Line 2: "Replace :"  [UniTextEditor]  [X]                          */
    /* ------------------------------------------------------------------ */
    replaceLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text,                (ULONG)LOC(MSG_REPLACE_LABEL),
        TAG_END);

    sb->replaceEditor = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,                  (ULONG)ID_REPLACE_EDITOR,
        ICA_TARGET,             (ULONG)TargetInstance,
        UTED_BevelStyle,        BVS_FIELD,
        UTED_PointSize,         pointSize,
        UTED_URPDrawContext,    (ULONG)buttonsDc,
        UTED_KeyMessageMode,    UKM_External, /* send rawkey/vanilla keys messages from window */
        // UTED_AddFont,           (ULONG)"LiberationSans-Regular.ttf",
        // UTED_AddFont,           (ULONG)"NotoColorEmoji32.ttf",

        UTED_TextPen,           1UL,
        UTED_BgPen,             0UL,
        UTED_MaxDisplayLines,   1UL,
        UTED_NoLineFeed,        TRUE,
        UTED_WordWrap,          FALSE,
        UTED_LeftMargin,2,
        UTED_TopMargin,3,
        UTED_BottomMargin,1,
        UTED_LineSpacing,0,
        TAG_END);

    sb->replaceEraseBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   (ULONG)ID_REPLACE_ERASE,
        GA_Text, (ULONG)LOC(MSG_SEARCH_ERASE),
        GA_RelVerify,TRUE, /* needed to receive WMHI_GADGETUP */
        TAG_END);

    replaceLine = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AlignLabels,(ULONG)searchLine,
        // LAYOUT_AddChild,    (ULONG)replaceLabel,
        //     CHILD_WeightedWidth, 0,
        //     CHILD_MinWidth,80,
        LAYOUT_AddChild,    (ULONG)sb->replaceEditor,
            CHILD_WeightedWidth, 1,
            CHILD_Label, (ULONG)replaceLabel,
        LAYOUT_AddChild,    (ULONG)sb->replaceEraseBtn,
            CHILD_WeightedWidth, 0,
        TAG_END);

    /* ------------------------------------------------------------------ */
    /* Line 3: [Find Next] [Find Previous] [Replace] [Replace All]        */
    /*         [Case sensitive checkbox]                                   */
    /* ------------------------------------------------------------------ */
    sb->findNextBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   (ULONG)ID_SEARCH_FIND_NEXT,
        GA_Text, (ULONG)LOC(MSG_SEARCH_FIND_NEXT),
        GA_RelVerify,TRUE, /* needed to receive WMHI_GADGETUP */
        TAG_END);

    sb->findPrevBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   (ULONG)ID_SEARCH_FIND_PREV,
        GA_Text, (ULONG)LOC(MSG_SEARCH_FIND_PREV),
        GA_RelVerify,TRUE, /* needed to receive WMHI_GADGETUP */
        TAG_END);

    sb->replaceBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   (ULONG)ID_SEARCH_REPLACE,
        GA_Text, (ULONG)LOC(MSG_SEARCH_REPLACE),
        GA_RelVerify,TRUE, /* needed to receive WMHI_GADGETUP */
        TAG_END);

    sb->replaceAllBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,   (ULONG)ID_SEARCH_REPLACE_ALL,
        GA_Text, (ULONG)LOC(MSG_SEARCH_REPLACE_ALL),
        GA_RelVerify,TRUE, /* needed to receive WMHI_GADGETUP */
        TAG_END);

    sb->caseSensCheck = (Object *)NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,            (ULONG)ID_SEARCH_CASE_SENS,
        GA_Text,          (ULONG)LOC(MSG_SEARCH_CASE_SENS),
        CHECKBOX_Checked, FALSE,
        TAG_END);

    // sb->closeBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
    //     GA_ID,   (ULONG)ID_SEARCH_CLOSE,
    //     GA_Text, (ULONG)LOC(MSG_SEARCH_CLOSE),
    //     TAG_END);

    buttonLine = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_InnerSpacing, 2,
        LAYOUT_AddChild,    (ULONG)sb->findNextBtn,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,    (ULONG)sb->findPrevBtn,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,    (ULONG)sb->replaceBtn,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,    (ULONG)sb->replaceAllBtn,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,    (ULONG)sb->caseSensCheck,
            CHILD_WeightedWidth, 0,
        // LAYOUT_AddChild,    (ULONG)NewObject(BUTTON_GetClass(), NULL,
        //                         GA_ReadOnly,        TRUE,
        //                         BUTTON_BevelStyle,  BVS_NONE,
        //                         BUTTON_Transparent, TRUE,
        //                         TAG_END),
        //     CHILD_WeightedWidth, 1,
        // LAYOUT_AddChild,    (ULONG)sb->closeBtn,
        //     CHILD_WeightedWidth, 0,
        TAG_END);

    /* ------------------------------------------------------------------ */
    /* Outer vertical layout                                              */
    /* ------------------------------------------------------------------ */
    sb->layout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_LeftSpacing,3,
        LAYOUT_RightSpacing,3,
        LAYOUT_TopSpacing,3,
        LAYOUT_BottomSpacing,3,
        LAYOUT_AddChild,      (ULONG)searchLine,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)replaceLine,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)buttonLine,
            CHILD_WeightedHeight, 0,

        TAG_END);

    /* --- BOOPSI window object --- */
    sb->windowObj = NewObject(WINDOW_GetClass(), NULL,
                         WA_Left,   100,
                         WA_Top,    60,
      WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_MENUPICK | IDCMP_RAWKEY | IDCMP_VANILLAKEY |
                  IDCMP_GADGETDOWN | IDCMP_GADGETUP /*| IDCMP_MOUSEMOVE*/ | IDCMP_NEWSIZE,
        WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                  WFLG_SIZEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
                         WA_Title,  (ULONG)LOC(MSG_SEARCH_LABEL),
                         WINDOW_ParentGroup, (ULONG)sb->layout,
                         TAG_END);
    if (!sb->windowObj) {
        DisposeObject(sb->layout);
        sb->layout = NULL;
        return FALSE;
    }

    return TRUE;
}

void EgSearchBox_Dispose(EgSearchBox *sb)
{
    if (sb->windowObj) {

        EgSearchBox_Close(sb);

        DisposeObject(sb->windowObj);
        sb->windowObj = NULL;
    }
}



void EgSearchBox_Open(EgSearchBox *sb)
{
    if (!sb || !sb->windowObj) return;
    if (sb->window)
    {
        WindowToFront(sb->window);
        ActivateWindow(sb->window);
        return; /* already open */
    }

    if (CurrentMainScreen) {
        SetAttrs(sb->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    if (sb->width > 0) {
        SetAttrs(sb->windowObj,
                 WA_Left,   (ULONG)sb->left,
                 WA_Top,    (ULONG)sb->top,
                 WA_Width,  (ULONG)sb->width,
                 WA_Height, (ULONG)sb->height,
                 TAG_END);
    }

    sb->window = (struct Window *)DoMethod(sb->windowObj, WM_OPEN, NULL);
    if(sb->window)
        EgMenu_Create(&sb->menu,CurrentMainScreen,sb->window,&app->appSettings);

    /* sort of activate the search field */
    app->activeEditorObj = sb->searchEditor;
}

void EgSearchBox_Close(EgSearchBox *sb)
{
    if (!sb || !sb->windowObj || !sb->window) return;

    GetAttr(WA_Left,   sb->windowObj, (ULONG *)&sb->left);
    GetAttr(WA_Top,    sb->windowObj, (ULONG *)&sb->top);
    GetAttr(WA_Width,  sb->windowObj, (ULONG *)&sb->width);
    GetAttr(WA_Height, sb->windowObj, (ULONG *)&sb->height);

    if(sb->window && sb->menu.menu)
        EgMenu_Close( &sb->menu,sb->window );
    DoMethod(sb->windowObj, WM_CLOSE, NULL);
    sb->window = NULL;
}

BOOL EgSearchBox_HandleInput(EgSearchBox *sb)
{
    ULONG result;

    if (!sb || !sb->windowObj) return FALSE;
    if (!sb->window) return TRUE; /* window closed, that's fine */

    while ((result = DoMethod(sb->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                EgSearchBox_Close(sb);
                return TRUE;
            case WMHI_NEWSIZE:
                if (sb->searchEditor)
                    RefreshGList((struct Gadget *)sb->searchEditor, sb->window, NULL, 1);
                if (sb->replaceEditor)
                    RefreshGList((struct Gadget *)sb->replaceEditor, sb->window, NULL, 1);
                break;
            case WMHI_ACTIVE:
            app->activeEditorObj == sb->searchEditor;
            break;
           case WMHI_RAWKEY:
            {
                ULONG qualifiers=0;
                GetAttr(WINDOW_Qualifier,sb->windowObj,&qualifiers);
                ULONG key = (result & 0x07f);
                ULONG isUp = (result & 0x080);

                // if(key == 0x45) {
                //     EgSearchBox_Close(sb);
                //     return TRUE;
                // }
                if(app->activeEditorObj == sb->searchEditor ||
                    app->activeEditorObj == sb->replaceEditor)
                {
                    if (!EmojiBox_HandleFKey(app, key, qualifiers,sb->window))
                    {
                        SetGadgetAttrs(app->activeEditorObj,
                            sb->window,NULL,
                            UTED_PutRawKey,key|(qualifiers<<16),TAG_END);
                    }
                }
            }
            break;
            case WMHI_VANILLAKEY:
            {
                ULONG key = (result & 0x0FF);
                ULONG qualifiers=0;

                if(key == 0x1b) { /* esc in vanilla */
                    EgSearchBox_Close(sb);
                    return TRUE;
                }
                 GetAttr(WINDOW_Qualifier,sb->windowObj,&qualifiers);

                if(app->activeEditorObj == sb->searchEditor ||
                    app->activeEditorObj == sb->replaceEditor)
                {
                    SetGadgetAttrs(app->activeEditorObj,
                        sb->window,NULL,
                         UTED_PutVanillaKey, key, TAG_END);
                }
            }
            break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;
                switch(gadId)
                {
                    case ID_SEARCH_ERASE:
                        if(sb->searchEditor)
                            SetGadgetAttrs(sb->searchEditor,sb->window,NULL,
                                        UTED_Text,(ULONG)"",TAG_END);
                    break;
                    case ID_REPLACE_ERASE:
                        if(sb->replaceEditor)
                            SetGadgetAttrs(sb->replaceEditor,sb->window,NULL,
                                        UTED_Text,(ULONG)"",TAG_END);
                    break;
                     case ID_SEARCH_FIND_NEXT:
                     {
                        Action_NavFindNext(app);
                     }
                    break;
                     case ID_SEARCH_FIND_PREV:
                    {
                        Action_NavFindPrev(app);
                    }
                    break;
                     case ID_SEARCH_REPLACE:
                     {
                        Action_NavReplace(app);
                     }
                    break;
                     case ID_SEARCH_REPLACE_ALL:
                     {
                        Action_NavReplaceAll(app);
                     }
                    break;
                     case ID_SEARCH_CASE_SENS:
                    break;

                }
                break;
            }
            case WMHI_MENUPICK:
            {
                /* Dispatch menu selection to action system */
                UWORD menuCode = (UWORD)(result & WMHI_MENUMASK);
                if (menuCode != MENUNULL) {
                    LONG actionID = EgMenu_ToActionID(&app->mainwindow.menu,
                                                      menuCode);
                    if (actionID >= 0) {
                        EgAction_Execute((ULONG)actionID,app);
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

ULONG EgSearchBox_GetSignalMask(EgSearchBox *sb)
{
// WINDOW_SigMask?
    if (!sb || !sb->window) return 0;
    return (1L << sb->window->UserPort->mp_SigBit);
}

/* return the current search text, DO NO KEEP POINTER ! */
const char *EgSearchBox_GetUTF8SearchString(EgSearchBox *sb)
{
    const char *text=NULL;
    if(!sb || !sb->searchEditor) return NULL;

    SetAttrs(sb->searchEditor,UTED_LineTextToGet,0,TAG_END);
    GetAttr(UTED_LineUTF8TextBuffer,sb->searchEditor,(ULONG*)&text);

    return text;
}

/* return the current replace text, DO NO KEEP POINTER ! */
const char *EgSearchBox_GetUTF8ReplaceString(EgSearchBox *sb)
{
    const char *text=NULL;
    if(!sb || !sb->replaceEditor) return NULL;

    SetAttrs(sb->replaceEditor,UTED_LineTextToGet,0,TAG_END);
    GetAttr(UTED_LineUTF8TextBuffer,sb->replaceEditor,(ULONG*)&text);

    return text;
}
int EgSearchBox_GetCaseSensitive(EgSearchBox *sb)
{
    ULONG checked=FALSE;
    if(!sb || !sb->caseSensCheck) return FALSE;

    GetAttr(CHECKBOX_Checked,sb->caseSensCheck,&checked);
    return checked;
}
