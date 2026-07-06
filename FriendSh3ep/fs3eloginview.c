/*
 * fs3eloginview.c - Login sub-window layout for FriendSh3ep.
 *
 * See fs3eloginview.h. Pattern adapted from EmojiGear/egsearchbox.c:
 * a self-contained BOOPSI window+layout subtree with Create/Dispose/
 * Open/Close/HandleInput/GetSignalMask.
 */

#include <string.h>

#include <proto/intuition.h>
#include <proto/alib.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/string.h>
#include <gadgets/string.h>

// #include <proto/unitexteditor.h>
// #include <gadgets/unitexteditor.h>

#include <proto/window.h>
#include <classes/window.h>

#include <intuition/icclass.h>

#include "fs3eloginview.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3egadgetid.h"
#include "fs3elocale.h"

/* A transparent, read-only button used as a layout spacer. */
static Object *Spacer(void)
{
    return (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,        TRUE,
        BUTTON_BevelStyle,  BVS_NONE,
        BUTTON_Transparent, TRUE,
        TAG_END);
}

/* Build one "Label: [editor]" row, sharing a URPDrawContext across fields. */
static Object *MakeFieldEditor(ULONG gadId)
{
    Object *editor;

    editor = (Object *)NewObject(STRING_GetClass(), NULL,
        GA_ID,                  gadId,
        ICA_TARGET,             (ULONG)TargetInstance,
        GA_TabCycle, TRUE,
        // /* The gadget keeps BOOPSI activation and handles keys itself,
        //  * so this sub-window does not need to track/forward focus. */
        // UTED_KeyMessageMode,    UKM_Internal,
        // UTED_BevelStyle,        BVS_FIELD,
        // UTED_PointSize,         pointSize,
        // UTED_TextPen,           1UL,
        // UTED_BgPen,             0UL,
        // UTED_MaxDisplayLines,   1UL,
        // UTED_NoLineFeed,        TRUE,
        // UTED_WordWrap,          FALSE,
        // UTED_LeftMargin,        2,
        // UTED_TopMargin,         3,
        // UTED_BottomMargin,      1,
        // UTED_LineSpacing,       0,
        // (*sharedDc) ? UTED_URPDrawContext : UTED_AddFont,
        // (*sharedDc) ? *sharedDc : (ULONG)"LiberationSans-Regular.ttf",
        TAG_END);

    // if (editor && !*sharedDc)
    //     GetAttr(UTED_URPDrawContext, editor, sharedDc);

    return editor;
}

BOOL FS3ELoginView_Create(FS3ELoginView *lv, ULONG pointSize)
{
    Object *serverLabel, *userLabel, *codeLabel;
    Object *formGroup, *centerRow, *outerCol;

    {
        LONG sl = lv->left, st = lv->top, sw = lv->width, sh = lv->height;
        memset(lv, 0, sizeof(*lv));
        lv->left = sl; lv->top = st; lv->width = sw; lv->height = sh;
    }

    lv->serverEditor = MakeFieldEditor(GID_LOGIN_SERVER_EDITOR);
    lv->userEditor   = MakeFieldEditor(GID_LOGIN_USER_EDITOR);
    lv->codeEditor   = MakeFieldEditor(GID_LOGIN_CODE_EDITOR);

    if (!lv->serverEditor || !lv->userEditor || !lv->codeEditor)
        return FALSE;

    serverLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_LOGIN_SERVER),
        TAG_END);
    userLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_LOGIN_USER),
        TAG_END);
    codeLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_LOGIN_CODE),
        TAG_END);

    lv->loginBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_LOGIN_LOGIN_BUTTON,
        GA_RelVerify, TRUE,
        ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)LOC(MSG_LOGIN_LOGIN),
        GA_TabCycle, TRUE,
        TAG_END);
    if (!lv->loginBtn) return FALSE;

    /* ------------------------------------------------------------------ */
    /* Centered named group: "Login to Mastodon"                          */
    /* ------------------------------------------------------------------ */
    formGroup = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_LOGIN_TITLE),

        LAYOUT_BackFill,     NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,

        LAYOUT_AddChild,      (ULONG)lv->serverEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)serverLabel,

        LAYOUT_AddChild,      (ULONG)lv->userEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)userLabel,

        LAYOUT_AddChild,      (ULONG)lv->codeEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)codeLabel,

        LAYOUT_AddChild,      (ULONG)lv->loginBtn,
            CHILD_WeightedHeight, 0,

        TAG_END);
    if (!formGroup) return FALSE;

    /* Center the group horizontally and vertically in the window. */
    centerRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,    (ULONG)Spacer(),
            CHILD_WeightedWidth, 1,
        LAYOUT_AddChild,    (ULONG)formGroup,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      260,
        LAYOUT_AddChild,    (ULONG)Spacer(),
            CHILD_WeightedWidth, 1,
        TAG_END);

    outerCol = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_AddChild,    (ULONG)Spacer(),
            CHILD_WeightedHeight, 1,
        LAYOUT_AddChild,    (ULONG)centerRow,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)Spacer(),
            CHILD_WeightedHeight, 1,
        TAG_END);
    if (!outerCol) return FALSE;

    lv->layout = outerCol;

    lv->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   160,
        WA_Top,    80,
        WA_Width,  320,
        WA_Height, 200,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE,
        WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET | WFLG_SIZEGADGET |
                   WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WA_Title,  (ULONG)LOC(MSG_LOGIN_TITLE),
        WINDOW_ParentGroup, (ULONG)lv->layout,
        TAG_END);
    if (!lv->windowObj) {
        DisposeObject(lv->layout);
        lv->layout = NULL;
        return FALSE;
    }

    return TRUE;
}

void FS3ELoginView_Dispose(FS3ELoginView *lv)
{
    if (!lv) return;

    if (lv->windowObj) {
        FS3ELoginView_Close(lv);
        DisposeObject(lv->windowObj);
        lv->windowObj = NULL;
    }
}

void FS3ELoginView_Open(FS3ELoginView *lv)
{
    if (!lv || !lv->windowObj) return;

    if (lv->window) {
        WindowToFront(lv->window);
        ActivateWindow(lv->window);
        return; /* already open */
    }

    if (CurrentMainScreen) {
        SetAttrs(lv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    if (lv->width > 0) {
        SetAttrs(lv->windowObj,
                 WA_Left,   (ULONG)lv->left,
                 WA_Top,    (ULONG)lv->top,
                 WA_Width,  (ULONG)lv->width,
                 WA_Height, (ULONG)lv->height,
                 TAG_END);
    }

    lv->window = (struct Window *)DoMethod(lv->windowObj, WM_OPEN, NULL);
}

void FS3ELoginView_Close(FS3ELoginView *lv)
{
    if (!lv || !lv->windowObj || !lv->window) return;

    GetAttr(WA_Left,   lv->windowObj, (ULONG *)&lv->left);
    GetAttr(WA_Top,    lv->windowObj, (ULONG *)&lv->top);
    GetAttr(WA_Width,  lv->windowObj, (ULONG *)&lv->width);
    GetAttr(WA_Height, lv->windowObj, (ULONG *)&lv->height);

    DoMethod(lv->windowObj, WM_CLOSE, NULL);
    lv->window = NULL;
}

BOOL FS3ELoginView_HandleInput(FS3ELoginView *lv)
{
    ULONG result;

    if (!lv || !lv->windowObj) return FALSE;
    if (!lv->window) return TRUE; /* closed, that's fine */

    while ((result = DoMethod(lv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3ELoginView_Close(lv);
                return TRUE;

            case WMHI_NEWSIZE:
                if (lv->serverEditor)
                    RefreshGList((struct Gadget *)lv->serverEditor, lv->window, NULL, 1);
                if (lv->userEditor)
                    RefreshGList((struct Gadget *)lv->userEditor, lv->window, NULL, 1);
                if (lv->codeEditor)
                    RefreshGList((struct Gadget *)lv->codeEditor, lv->window, NULL, 1);
                break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;

                BoopsiDelay_BeginMessage(DelayQueue, gadId);
                BoopsiDelay_AddTag(DelayQueue, WMHI_GADGETUP, 1);
                BoopsiDelay_EndMessage(DelayQueue);
                break;
            }

            default:
                break;
        }
    }

    return TRUE;
}

ULONG FS3ELoginView_GetSignalMask(FS3ELoginView *lv)
{
    if (!lv || !lv->window) return 0;
    return (1L << lv->window->UserPort->mp_SigBit);
}

// static const char *GetEditorUTF8Text(Object *editor)
// {
//     const char *text = NULL;

//     if (!editor) return NULL;

//     SetAttrs(editor, UTED_LineTextToGet, 0, TAG_END);
//     GetAttr(UTED_LineUTF8TextBuffer, editor, (ULONG *)&text);

//     return text;
// }

const char *FS3ELoginView_GetANSIServer(FS3ELoginView *lv)
{
    const char *text;
    if (!lv) return NULL;
    GetAttr(GA_Text, lv->serverEditor, (ULONG *)&text);
    return text;
}

const char *FS3ELoginView_GetANSIUser(FS3ELoginView *lv)
{
    const char *text;
    if (!lv) return NULL;
    GetAttr(GA_Text, lv->userEditor, (ULONG *)&text);
    return text;
}

const char *FS3ELoginView_GetANSICode(FS3ELoginView *lv)
{
    const char *text;
    if (!lv) return NULL;
    GetAttr(GA_Text, lv->codeEditor, (ULONG *)&text);
    return text;
}
