/*
 * fs3eloginview.c - Login sub-window layout for FriendSh3ep.
 *
 * Two-phase OAuth layout:
 *   Phase 1 (always enabled):  Server, User, [Connect button]
 *   Phase 2 (disabled until URL arrives):
 *       Instruction label, URL display editor, Code field, [Submit Code button]
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

#include <proto/window.h>
#include <classes/window.h>

#include <intuition/icclass.h>

#include <proto/texteditor.h>
#include <gadgets/texteditor.h>

#include "fs3eloginview.h"
#include "clipboard.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3egadgetid.h"
#include "fs3elocale.h"

#define INSTRUCT_INITIAL "Click Connect to get the authorization URL."
#define INSTRUCT_READY   "Open this URL in your browser, then paste the code below, it has been copied to clipboard:"

/* Transparent spacer to push content. */
static Object *Spacer(void)
{
    return (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,        TRUE,
        BUTTON_BevelStyle,  BVS_NONE,
        BUTTON_Transparent, TRUE,
        TAG_END);
}

/* Single-line editable string gadget. */
static Object *MakeEditor(ULONG gadId)
{
    return (Object *)NewObject(STRING_GetClass(), NULL,
        GA_ID,       gadId,
        ICA_TARGET,  (ULONG)TargetInstance,
        GA_TabCycle, TRUE,
        TAG_END);
}

/* Read-only string gadget (user can activate + AMIGA-C to copy, not edit). */
static Object *MakeReadOnlyEditor(ULONG gadId, BOOL disabled)
{
    return (Object *)NewObject(TEXTEDITOR_GetClass(), NULL,
        GA_ID,            gadId,
        GA_ReadOnly,      TRUE,
        GA_Disabled,      (ULONG)disabled,
        TAG_END);
}

/* No-bevel read-only button used as a dynamic text label. */
static Object *MakeInstructLabel(BOOL disabled)
{
    return (Object *)NewObject(TEXTEDITOR_GetClass(), NULL,
        GA_ReadOnly,        TRUE,
        GA_Disabled,        (ULONG)disabled,
        GA_TEXTEDITOR_BevelStyle,  BVS_NONE,
      //  BUTTON_Transparent, TRUE,
        GA_Text,            (ULONG)INSTRUCT_INITIAL,
        TAG_END);
}

BOOL FS3ELoginView_Create(FS3ELoginView *lv, ULONG pointSize)
{
    Object *serverLabel, *userLabel, *codeLabel, *urlLabel;
    Object *formGroup, *centerRow, *outerCol;
    Object *divider;

    {
        LONG sl = lv->left, st = lv->top, sw = lv->width, sh = lv->height;
        memset(lv, 0, sizeof(*lv));
        lv->left = sl; lv->top = st; lv->width = sw; lv->height = sh;
    }

    /* --- Phase 1 gadgets (always enabled) --- */
    lv->serverEditor = MakeEditor(GID_LOGIN_SERVER_EDITOR);
    lv->userEditor   = MakeEditor(GID_LOGIN_USER_EDITOR);
    if (!lv->serverEditor || !lv->userEditor) return FALSE;

    lv->loginBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_LOGIN_LOGIN_BUTTON,
        GA_RelVerify, TRUE,
        ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)LOC(MSG_LOGIN_LOGIN),
        GA_TabCycle,  TRUE,
        TAG_END);
    if (!lv->loginBtn) return FALSE;

    serverLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_LOGIN_SERVER), TAG_END);
    userLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_LOGIN_USER), TAG_END);

    /* --- Phase 2 gadgets (disabled until URL arrives) --- */
    lv->urlInstructLabel = MakeInstructLabel(TRUE);
    if (!lv->urlInstructLabel) return FALSE;

    lv->urlEditor = MakeReadOnlyEditor(GID_LOGIN_URL_EDITOR, TRUE);
    if (!lv->urlEditor) return FALSE;

    lv->codeEditor = MakeEditor(GID_LOGIN_CODE_EDITOR);
    if (!lv->codeEditor) return FALSE;
    SetAttrs(lv->codeEditor, GA_Disabled, TRUE, TAG_END);

    lv->submitCodeBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_LOGIN_SUBMIT_CODE_BUTTON,
        GA_RelVerify, TRUE,
        ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)"Submit Code",
        GA_Disabled,  TRUE,
        GA_TabCycle,  TRUE,
        TAG_END);
    if (!lv->submitCodeBtn) return FALSE;

    urlLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)"URL:", TAG_END);
    codeLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_LOGIN_CODE), TAG_END);

    /* thin horizontal line between the two phases */
    divider = Spacer();

    /* ------------------------------------------------------------------ */
    /* Centered named group                                                */
    /* ------------------------------------------------------------------ */
    formGroup = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_GROUP,
        LAYOUT_Label,       (ULONG)LOC(MSG_LOGIN_TITLE),
        LAYOUT_BackFill,    NULL,
        LAYOUT_SpaceOuter,  TRUE,
        LAYOUT_SpaceInner,  TRUE,

        /* --- Phase 1 --- */
        LAYOUT_AddChild,      (ULONG)lv->serverEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)serverLabel,

        LAYOUT_AddChild,      (ULONG)lv->userEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)userLabel,

        LAYOUT_AddChild,      (ULONG)lv->loginBtn,
            CHILD_WeightedHeight, 0,

        /* thin spacer between phases */
        LAYOUT_AddChild,      (ULONG)divider,
            CHILD_WeightedHeight, 0,
            CHILD_MinHeight,      4,
            CHILD_MaxHeight,      4,

        /* --- Phase 2 --- */
        LAYOUT_AddChild,      (ULONG)lv->urlInstructLabel,
            CHILD_WeightedHeight, 0,

        LAYOUT_AddChild,      (ULONG)lv->urlEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)urlLabel,

        LAYOUT_AddChild,      (ULONG)lv->codeEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)codeLabel,

        LAYOUT_AddChild,      (ULONG)lv->submitCodeBtn,
            CHILD_WeightedHeight, 0,

        TAG_END);
    if (!formGroup) return FALSE;

    centerRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,    (ULONG)Spacer(),
            CHILD_WeightedWidth, 1,
        LAYOUT_AddChild,    (ULONG)formGroup,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      300,
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
        WA_Width,  360,
        WA_Height, 280,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE,
        WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                   WFLG_SIZEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
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
        return;
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
    if (!lv->window) return TRUE;

    while ((result = DoMethod(lv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3ELoginView_Close(lv);
                return TRUE;

            case WMHI_NEWSIZE:
                /* Refresh string gadgets after resize (layout handles buttons). */
                if (lv->serverEditor)
                    RefreshGList((struct Gadget *)lv->serverEditor, lv->window, NULL, 1);
                if (lv->userEditor)
                    RefreshGList((struct Gadget *)lv->userEditor,   lv->window, NULL, 1);
                if (lv->urlEditor)
                    RefreshGList((struct Gadget *)lv->urlEditor,    lv->window, NULL, 1);
                if (lv->codeEditor)
                    RefreshGList((struct Gadget *)lv->codeEditor,   lv->window, NULL, 1);
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

/* Called when LOGIN_START reply arrives. Enables the phase-2 section
 * and displays the authorize URL. */
void FS3ELoginView_SetAuthorizeUrl(FS3ELoginView *lv, const char *url)
{
    if (!lv || !url) return;

    Clipboard_WriteText(url);

    if (lv->window) {
        SetGadgetAttrs((struct Gadget *)lv->urlInstructLabel, lv->window, NULL,
            GA_Disabled, FALSE,
            GA_TEXTEDITOR_Contents,     (ULONG)INSTRUCT_READY,
            TAG_DONE);
      //  RefreshGList((struct Gadget *)lv->urlInstructLabel, lv->window, NULL, 1);

        SetGadgetAttrs((struct Gadget *)lv->urlEditor, lv->window, NULL,
            GA_Disabled,      FALSE,
            GA_TEXTEDITOR_Contents,  (ULONG)url,
            TAG_DONE);
       // RefreshGList((struct Gadget *)lv->urlEditor, lv->window, NULL, 1);

        SetGadgetAttrs((struct Gadget *)lv->codeEditor, lv->window, NULL,
            GA_Disabled, FALSE,
            TAG_DONE);
     //   RefreshGList((struct Gadget *)lv->codeEditor, lv->window, NULL, 1);

        SetGadgetAttrs((struct Gadget *)lv->submitCodeBtn, lv->window, NULL,
            GA_Disabled, FALSE,
            TAG_DONE);
      //  RefreshGList((struct Gadget *)lv->submitCodeBtn, lv->window, NULL, 1);
    } else {
        /* Window closed — update attrs so they're correct when re-opened. */
        SetAttrs(lv->urlInstructLabel,
            GA_Disabled, FALSE,
            GA_TEXTEDITOR_Contents,     (ULONG)INSTRUCT_READY,
            TAG_END);
        SetAttrs(lv->urlEditor,
            GA_Disabled,     FALSE,
            GA_TEXTEDITOR_Contents, (ULONG)url,
            TAG_END);
        SetAttrs(lv->codeEditor,    GA_Disabled, FALSE, TAG_END);
        SetAttrs(lv->submitCodeBtn, GA_Disabled, FALSE, TAG_END);
    }
}

/* Empties the server/user entry fields -- called once credentials are
 * confirmed (a saved account loaded, or a fresh LOGIN_FINISH succeeds), so
 * there's nothing pre-filled left to accidentally resubmit and trigger a
 * fresh re-auth (see GID_LOGIN_LOGIN_BUTTON's "already connected" branch
 * in friendsh3ep.c). */
void FS3ELoginView_ClearFields(FS3ELoginView *lv)
{
    if (!lv) return;

    if (lv->window) {
        if (lv->serverEditor)
            SetGadgetAttrs((struct Gadget *)lv->serverEditor, lv->window, NULL,
                STRINGA_TextVal, (ULONG)"", TAG_DONE);
        if (lv->userEditor)
            SetGadgetAttrs((struct Gadget *)lv->userEditor, lv->window, NULL,
                STRINGA_TextVal, (ULONG)"", TAG_DONE);
    } else {
        if (lv->serverEditor)
            SetAttrs(lv->serverEditor, STRINGA_TextVal, (ULONG)"", TAG_END);
        if (lv->userEditor)
            SetAttrs(lv->userEditor, STRINGA_TextVal, (ULONG)"", TAG_END);
    }
}

const char *FS3ELoginView_GetANSIServer(FS3ELoginView *lv)
{
    const char *text = NULL;
    if (!lv || !lv->serverEditor) return NULL;
    GetAttr(STRINGA_TextVal, lv->serverEditor, (ULONG *)&text);
    return text;
}

const char *FS3ELoginView_GetANSIUser(FS3ELoginView *lv)
{
    const char *text = NULL;
    if (!lv || !lv->userEditor) return NULL;
    GetAttr(STRINGA_TextVal, lv->userEditor, (ULONG *)&text);
    return text;
}

const char *FS3ELoginView_GetANSICode(FS3ELoginView *lv)
{
    const char *text = NULL;
    if (!lv || !lv->codeEditor) return NULL;
    GetAttr(STRINGA_TextVal, lv->codeEditor, (ULONG *)&text);
    return text;
}
