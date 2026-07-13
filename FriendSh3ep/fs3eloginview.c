/*
 * fs3eloginview.c - Login sub-window layout for FriendSh3ep.
 *
 * Two-phase OAuth layout:
 *   Phase 1 (always enabled):  Server, User, [Connect button]
 *   Phase 2 (disabled until URL arrives):
 *       Instruction label, URL display editor, Code field, [Submit Code button]
 */

#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/unibutton.h>
#include <gadgets/unibutton.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/string.h>
#include <gadgets/string.h>

#include <proto/window.h>
#include <classes/window.h>

#include <intuition/icclass.h>

#include <proto/texteditor.h>
#include <gadgets/texteditor.h>

#include <proto/listbrowser.h>
#include <gadgets/listbrowser.h>

#include "fs3eloginview.h"
#include "clipboard.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3egadgetid.h"
#include "fs3elocale.h"

#include <stdio.h>

#define INSTRUCT_INITIAL "Fill server & user, then Click Login\n to get the authorization URL."
#define INSTRUCT_READY   "URL copied to clipboard.\n Open it in a browser, authenticate App,\nthen paste the code back below and submit."

/* Accounts list columns: server, user. Weighted (not fixed) so both
 * share the gadget's width proportionally as it resizes. */
static struct ColumnInfo accListColumns[] = {
    { 100, (STRPTR)"Server", CIF_WEIGHTED },
    { 80,  (STRPTR)"User",   CIF_WEIGHTED },
    { -1,  (STRPTR)~0,       (ULONG)-1 }
};

/* Max characters copied into a node's column text (see LBNCA_CopyText
 * below) -- generous enough for any realistic server hostname or acct. */
#define ACCLIST_MAXCHARS 255

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


BOOL FS3ELoginView_Create(FS3ELoginView *lv, struct URPDrawContext *textDC)
{
    Object *serverLabel, *userLabel, *codeLabel, *urlLabel;
    Object *formGroup, *centerRow, *outerCol,*acclistGroup;
    Object *logHgr1,*logHgr2;

    {
        LONG sl = lv->left, st = lv->top, sw = lv->width, sh = lv->height;
        memset(lv, 0, sizeof(*lv));
        lv->left = sl; lv->top = st; lv->width = sw; lv->height = sh;
    }
    NewList(&lv->accList);

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
        LABEL_Text, (ULONG)LOC(MSG_LOGIN_USERORMAIL), TAG_END);

    /* --- Phase 2 gadgets (disabled until URL arrives) --- */
   // printf("login view dc:%08x\n",(int)textDC);
    lv->urlInstructLabel = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        UBT_URPDrawContext,(ULONG)textDC,
        GA_ReadOnly,        TRUE,
        UBT_BevelStyle,  BVS_NONE,
        TAG_END);

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
 //  divider = Spacer();


    /* - - -  - -*/
    {
        Object *vg = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,

        LAYOUT_AddChild,      (ULONG)lv->serverEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)serverLabel,

        LAYOUT_AddChild,      (ULONG)lv->userEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)userLabel,

        TAG_END
        );

    logHgr1 = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,
        LAYOUT_AddChild,      (ULONG)vg,
            CHILD_WeightedWidth,1,
        LAYOUT_AddChild,      (ULONG)lv->loginBtn,
            CHILD_WeightedWidth, 0,

        TAG_END
        );
    }

    {
        Object *vg = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,

        LAYOUT_AddChild,      (ULONG)lv->urlEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)urlLabel,
            CHILD_MaxHeight,    16,

        LAYOUT_AddChild,      (ULONG)lv->codeEditor,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)codeLabel,

        TAG_END
        );

    logHgr2 = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,  BVS_NONE,
        LAYOUT_SpaceOuter,  FALSE,
        LAYOUT_SpaceInner,  FALSE,
        LAYOUT_AddChild,      (ULONG)vg,
            CHILD_WeightedWidth,1,

        LAYOUT_AddChild,      (ULONG)lv->submitCodeBtn,
            CHILD_WeightedWidth, 0,

        TAG_END
        );
    }



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
       LAYOUT_AddChild, (ULONG) logHgr1,
     //   CHILD_WeightedHeight, 0,
        // LAYOUT_AddChild,      (ULONG)lv->serverEditor,
        //     CHILD_WeightedHeight, 0,
        //     CHILD_Label,          (ULONG)serverLabel,

        // LAYOUT_AddChild,      (ULONG)lv->userEditor,
        //     CHILD_WeightedHeight, 0,
        //     CHILD_Label,          (ULONG)userLabel,

        // LAYOUT_AddChild,      (ULONG)lv->loginBtn,
        //     CHILD_WeightedHeight, 0,

        /* thin spacer between phases */
        // LAYOUT_AddChild,      (ULONG)divider,
        //     CHILD_WeightedHeight, 0,
        //     CHILD_MinHeight,      4,
        //     CHILD_MaxHeight,      4,

        /* --- Phase 2 --- */
        LAYOUT_AddChild,      (ULONG)lv->urlInstructLabel,
        //    CHILD_WeightedHeight, 0,

       LAYOUT_AddChild, (ULONG) logHgr2,
       // CHILD_WeightedHeight, 0,

        TAG_END);
    if (!formGroup) return FALSE;

    // centerRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
    //     LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
    //     // LAYOUT_AddChild,    (ULONG)Spacer(),
    //     //     CHILD_WeightedWidth, 1,
    //     LAYOUT_AddChild,    (ULONG)formGroup,
    //     //    CHILD_WeightedWidth, 0,
    //       //  CHILD_MinWidth,      300,
    //     // LAYOUT_AddChild,    (ULONG)Spacer(),
    //     //     CHILD_WeightedWidth, 1,
    //     TAG_END);

    lv->acclistBrowser = (Object *)NewObject(LISTBROWSER_GetClass(), NULL,
        GA_ID,                    (ULONG)GID_LOGIN_ACCOUNTS_LIST,
        ICA_TARGET,                (ULONG)TargetInstance,
        GA_RelVerify,               TRUE,
        LISTBROWSER_ColumnInfo,    (ULONG)accListColumns,
        LISTBROWSER_ColumnTitles,   TRUE,
        LISTBROWSER_Labels,        (ULONG)&lv->accList,
        LISTBROWSER_Selected,      (ULONG)-1,
        LISTBROWSER_ShowSelected,   TRUE,
        LISTBROWSER_MinVisible,     3,
        TAG_END);
    if (!lv->acclistBrowser) return FALSE;

    acclistGroup = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,  BVS_GROUP,
        LAYOUT_Label,       (ULONG)LOC(MSG_MENU_ACCOUNTS),
        LAYOUT_BackFill,    NULL,
        LAYOUT_SpaceOuter,  TRUE,
        LAYOUT_SpaceInner,  TRUE,

        LAYOUT_AddChild,    (ULONG)lv->acclistBrowser,
            CHILD_WeightedHeight, 1,
        TAG_END);


    outerCol = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
        // LAYOUT_AddChild,    (ULONG)Spacer(),
        //     CHILD_WeightedHeight, 1,
        LAYOUT_AddChild,    (ULONG)formGroup,
            CHILD_WeightedHeight, 0,
         LAYOUT_AddChild,    (ULONG)acclistGroup,
            CHILD_WeightedHeight, 1,
        // LAYOUT_AddChild,    (ULONG)Spacer(),
        //     CHILD_WeightedHeight, 1,
        TAG_END);
    if (!outerCol) return FALSE;

    lv->layout = outerCol;

    lv->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   160,
        WA_Top,    80,
        WA_Width,  360,
        WA_Height, 340,
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

    /* acclistBrowser itself was disposed along with windowObj above, but
     * the nodes we AllocListBrowserNode'd into lv->accList are not owned
     * by the gadget -- free them separately (safe with an empty list). */
    FreeListBrowserList(&lv->accList);
    NewList(&lv->accList);
}

void FS3ELoginView_Open(FS3ELoginView *lv)
{
    if (!lv || !lv->windowObj) return;

    if(lv->urlInstructLabel)
    {
        SetAttrs(lv->urlInstructLabel,
        GA_Text,            (ULONG)INSTRUCT_INITIAL,
        TAG_END);
    }

    if (lv->window) {
        WindowToFront(lv->window);
        ActivateWindow(lv->window);
        DoMethod(lv->windowObj, WM_RETHINK);
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
      DoMethod(lv->windowObj, WM_RETHINK);
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
                if (lv->acclistBrowser)
                    RefreshGList((struct Gadget *)lv->acclistBrowser, lv->window, NULL, 1);
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
            GA_Text,     (ULONG)INSTRUCT_READY,
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

         DoMethod(lv->windowObj, WM_RETHINK);
      //  RefreshGList((struct Gadget *)lv->submitCodeBtn, lv->window, NULL, 1);
    } else {
        /* Window closed — update attrs so they're correct when re-opened. */
        SetAttrs(lv->urlInstructLabel,
            GA_Text,     (ULONG)INSTRUCT_READY,
            TAG_END);
        SetAttrs(lv->urlEditor,
            GA_Disabled,     FALSE,
            GA_TEXTEDITOR_Contents, (ULONG)url,
            TAG_END);
        SetAttrs(lv->codeEditor,    GA_Disabled, FALSE, TAG_END);
        SetAttrs(lv->submitCodeBtn, GA_Disabled, FALSE, TAG_END);
    }
}

/* Resets the whole form back to its pristine phase-1-only state -- called
 * once credentials are confirmed (a saved account loaded, or a fresh
 * LOGIN_FINISH succeeds). Empties server/user so there's nothing pre-filled
 * left to accidentally resubmit, AND empties+disables the phase-2 fields
 * (url/code/submit) -- their contents (authorize URL, pasted code) did
 * their job for that login and are now obsolete: with multi-account
 * support, this same window gets reopened to add another account, and a
 * leftover URL/code from the previous one must not look like it's still
 * live (see GID_LOGIN_LOGIN_BUTTON in friendsh3ep.c, which now always
 * starts a fresh OAuth flow rather than assuming "already connected" means
 * "log out first"). */
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
        if (lv->urlInstructLabel)
            SetGadgetAttrs((struct Gadget *)lv->urlInstructLabel, lv->window, NULL,
                GA_Text, (ULONG)INSTRUCT_INITIAL, TAG_DONE);
        if (lv->urlEditor)
            SetGadgetAttrs((struct Gadget *)lv->urlEditor, lv->window, NULL,
                GA_TEXTEDITOR_Contents, (ULONG)"",
                GA_Disabled,            TRUE,
                TAG_DONE);
        if (lv->codeEditor)
            SetGadgetAttrs((struct Gadget *)lv->codeEditor, lv->window, NULL,
                STRINGA_TextVal, (ULONG)"",
                GA_Disabled,     TRUE,
                TAG_DONE);
        if (lv->submitCodeBtn)
            SetGadgetAttrs((struct Gadget *)lv->submitCodeBtn, lv->window, NULL,
                GA_Disabled, TRUE, TAG_DONE);
        DoMethod(lv->windowObj, WM_RETHINK);
    } else {
        if (lv->serverEditor)
            SetAttrs(lv->serverEditor, STRINGA_TextVal, (ULONG)"", TAG_END);
        if (lv->userEditor)
            SetAttrs(lv->userEditor, STRINGA_TextVal, (ULONG)"", TAG_END);
        if (lv->urlInstructLabel)
            SetAttrs(lv->urlInstructLabel, GA_Text, (ULONG)INSTRUCT_INITIAL, TAG_END);
        if (lv->urlEditor)
            SetAttrs(lv->urlEditor,
                GA_TEXTEDITOR_Contents, (ULONG)"",
                GA_Disabled,            TRUE,
                TAG_END);
        if (lv->codeEditor)
            SetAttrs(lv->codeEditor,
                STRINGA_TextVal, (ULONG)"",
                GA_Disabled,     TRUE,
                TAG_END);
        if (lv->submitCodeBtn)
            SetAttrs(lv->submitCodeBtn, GA_Disabled, TRUE, TAG_END);
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

/* Rebuilds the accounts listbrowser from scratch. Detach -> free old nodes
 * -> build new ones -> reattach, per the listbrowser.gadget autodoc's rule
 * that a list already attached must be detached (LISTBROWSER_Labels, ~0)
 * before its nodes are touched.
 *
 * Deliberately does NOT also SetGadgetAttrs(LISTBROWSER_Selected, idx) on
 * reattach -- that attribute is OM_NOTIFY-applicable, and this function
 * runs from inside friendsh3ep.c's FS3EApp_SwitchAccount(), itself called
 * FROM a GID_LOGIN_ACCOUNTS_LIST notify. Setting it here fired a fresh
 * notify back through ICA_TARGET, re-entering FS3EApp_SwitchAccount()
 * before the first call had even returned -- confirmed in the wild as an
 * infinite switch-to-account-A / switch-to-account-B loop (two known
 * accounts is enough for it to alternate forever) that flooded the network
 * process with alternating requests. The node's own LBNA_Selected (set at
 * allocation below) is honored by AllocListBrowserNode when the node is
 * first added, and is enough to render the right row highlighted without
 * ever touching the notify-applicable top-level attribute. */
void FS3ELoginView_SetAccountsList(FS3ELoginView *lv,
                                    const FS3ELoginAccountRow *rows,
                                    ULONG count)
{
    ULONG i;

    if (!lv || !lv->acclistBrowser) return;

    if (lv->window) {
        SetGadgetAttrs((struct Gadget *)lv->acclistBrowser, lv->window, NULL,
            LISTBROWSER_Labels, (ULONG)~0, TAG_DONE);
    } else {
        SetAttrs(lv->acclistBrowser, LISTBROWSER_Labels, (ULONG)~0, TAG_END);
    }

    FreeListBrowserList(&lv->accList);
    NewList(&lv->accList);

    for (i = 0; i < count; i++) {
        /* LBNCA_CopyText must precede LBNCA_Text in the tag list (see the
         * listbrowser.gadget autodoc) -- it only takes effect on the
         * LBNCA_Text tag that comes after it. */
        struct Node *node = AllocListBrowserNode(2,
            LBNA_Column,        0,
                LBNCA_CopyText,   TRUE,
                LBNCA_MaxChars,   ACCLIST_MAXCHARS,
                LBNCA_Text,      (ULONG)(rows[i].server ? rows[i].server : ""),
            LBNA_Column,        1,
                LBNCA_CopyText,   TRUE,
                LBNCA_MaxChars,   ACCLIST_MAXCHARS,
                LBNCA_Text,      (ULONG)(rows[i].user ? rows[i].user : ""),
            LBNA_Selected,       (ULONG)rows[i].current,
            LBNA_UserData,       (ULONG)i,
            TAG_DONE);
        if (!node) continue;
        AddTail(&lv->accList, node);
    }

    if (lv->window) {
        SetGadgetAttrs((struct Gadget *)lv->acclistBrowser, lv->window, NULL,
            LISTBROWSER_Labels,     (ULONG)&lv->accList,
            TAG_DONE);
        RefreshGList((struct Gadget *)lv->acclistBrowser, lv->window, NULL, 1);
    } else {
        SetAttrs(lv->acclistBrowser,
            LISTBROWSER_Labels,     (ULONG)&lv->accList,
            TAG_END);
    }
}
