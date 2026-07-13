/*
 * fs3etootview.c - "New toot" sub-window layout for FriendSh3ep.
 *
 * See fs3etootview.h. Pattern adapted from EmojiGear/egsearchbox.c.
 */

#include <string.h>
#include <stdio.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/unitexteditor.h>
#include <gadgets/unitexteditor.h>

#include <proto/unibutton.h>
#include <gadgets/unibutton.h>

#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/gadtools.h>
#include <libraries/gadtools.h>

#include <intuition/icclass.h>

#include "fs3etootview.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3egadgetid.h"
#include "fs3elocale.h"

#include "friendsh3ep.h"

extern struct Library *ChooserBase;
extern struct Library *GadToolsBase;

/* nm_UserData values for the window-local "Toot" menu -- see
 * FS3ETootView_MenuCreate/FS3ETootView_HandleInput's WMHI_MENUPICK case.
 * Deliberately its own tiny local scheme rather than fs3emenu.c's
 * FS3EACTION_ / ACTION_UD encoding: every one of these actions is entirely
 * local to this window's own editors, never triggered from anywhere else
 * (unlike the main menu's items, which mirror title-bar buttons/actions),
 * so there's no shared action table to hook into. 0 is reserved (title
 * items carry no UserData). */
typedef enum {
    FS3ETMENU_CLEAR = 1,
    FS3ETMENU_UNDO,
    FS3ETMENU_REDO,
    FS3ETMENU_CUT,
    FS3ETMENU_COPY,
    FS3ETMENU_PASTE,
    FS3ETMENU_EMOJIBOX
} FS3ETootMenuID;

/* Builds and attaches the "Toot" menu (Clear/Undo/Redo/separator/Cut/
 * Copy UTF8/Paste UTF8/separator/Emoji Box) to window. Amiga-Z/Y/X/C/V/E
 * work automatically once attached -- GadTools/Intuition itself watches
 * for the Amiga qualifier plus a menu's nm_CommKey letter and delivers it
 * as an ordinary IDCMP_MENUPICK, exactly like a mouse pick of that item;
 * no separate raw-key handling needed (see WA_IDCMP below, IDCMP_MENUPICK
 * must be set for either kind of pick to arrive at all). */
static BOOL FS3ETootView_MenuCreate(FS3ETootView *tv, struct Screen *screen,
                                     struct Window *window)
{
    struct NewMenu nm[11];
    int n = 0;

    if (!tv || !screen || !window || !GadToolsBase) return FALSE;

    tv->menu           = NULL;
    tv->menuVisualInfo = NULL;

    tv->menuVisualInfo = GetVisualInfo(screen, TAG_END);
    if (!tv->menuVisualInfo) return FALSE;

#define ADD(t, l, k, u) do { \
        nm[n].nm_Type          = (t); \
        nm[n].nm_Label         = (STRPTR)(l); \
        nm[n].nm_CommKey       = (STRPTR)(k); \
        nm[n].nm_Flags         = 0; \
        nm[n].nm_MutualExclude = 0; \
        nm[n].nm_UserData      = (APTR)(ULONG)(u); \
        n++; \
    } while (0)

    ADD(NM_TITLE, LOC(MSG_TOOTMENU_TOOT),    0,   0);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_CLEAR),   0,   FS3ETMENU_CLEAR);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_UNDO),    "Z", FS3ETMENU_UNDO);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_REDO),    "Y", FS3ETMENU_REDO);
    ADD(NM_ITEM,  NM_BARLABEL,               0,   0);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_CUT),     "X", FS3ETMENU_CUT);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_COPY),    "C", FS3ETMENU_COPY);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_PASTE),   "V", FS3ETMENU_PASTE);
    ADD(NM_ITEM,  NM_BARLABEL,               0,   0);
    ADD(NM_ITEM,  LOC(MSG_TOOTMENU_EMOJIBOX),"E", FS3ETMENU_EMOJIBOX);
    ADD(NM_END,   NULL,                      0,   0);
#undef ADD

    tv->menu = CreateMenus(nm, TAG_END);
    if (!tv->menu) {
        FreeVisualInfo(tv->menuVisualInfo);
        tv->menuVisualInfo = NULL;
        return FALSE;
    }

    if (!LayoutMenus(tv->menu, tv->menuVisualInfo, GTMN_NewLookMenus, TRUE, TAG_END)) {
        FreeMenus(tv->menu);
        FreeVisualInfo(tv->menuVisualInfo);
        tv->menu           = NULL;
        tv->menuVisualInfo = NULL;
        return FALSE;
    }

    SetMenuStrip(window, tv->menu);
    return TRUE;
}

/* Detaches and frees the menu. Safe to call any time, attached or not. */
static void FS3ETootView_MenuClose(FS3ETootView *tv, struct Window *window)
{
    if (!tv) return;

    if (window && tv->menu)
        ClearMenuStrip(window);

    if (tv->menu) {
        FreeMenus(tv->menu);
        tv->menu = NULL;
    }
    if (tv->menuVisualInfo) {
        FreeVisualInfo(tv->menuVisualInfo);
        tv->menuVisualInfo = NULL;
    }
}

static const ULONG visibilityMsgIds[FS3ETOOT_NUM_VISIBILITIES] = {
    MSG_TOOT_VISIBILITY_PUBLIC, MSG_TOOT_VISIBILITY_UNLISTED,
    MSG_TOOT_VISIBILITY_PRIVATE, MSG_TOOT_VISIBILITY_DIRECT
};

/* "-" if maxChars is 0 (not yet confirmed by the server for the active
 * account -- see App.accountMaxChars in friendsh3ep.h), else its decimal
 * string. Used by both charCountLabel sprintf call sites below. */
static void FormatMaxChars(ULONG maxChars, char *buf, ULONG bufSize)
{
    if (maxChars > 0) {
        snprintf(buf, bufSize, "%lu", (unsigned long)maxChars);
    } else {
        strncpy(buf, "-", bufSize - 1);
        buf[bufSize - 1] = '\0';
    }
}

/* A transparent, read-only button used as a layout spacer. */
static Object *Spacer(void)
{
    return (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,        TRUE,
        BUTTON_BevelStyle,  BVS_NONE,
        BUTTON_Transparent, TRUE,
        TAG_END);
}

BOOL FS3ETootView_Create(FS3ETootView *tv, struct URPDrawContext *textDC)
{
    Object *bottomBar;

    int i;

    {
        LONG sl = tv->left, st = tv->top, sw = tv->width, sh = tv->height;
        memset(tv, 0, sizeof(*tv));
        tv->left = sl; tv->top = st; tv->width = sw; tv->height = sh;
    }

    /* ------------------------------------------------------------------ */
    /* Context message: "Creating a new toot" / "Replying to ..." --      */
    /* read-only, no-bevel UniButton, same pattern as fs3eloginview.c's   */
    /* urlInstructLabel. Supports multi-line/Unicode text if ever needed. */
    /* ------------------------------------------------------------------ */
    tv->contextMessage = (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        UBT_URPDrawContext, (ULONG)textDC,
        GA_ReadOnly,        TRUE,
        UBT_BevelStyle,     BVS_NONE,
        GA_Text,            (ULONG)LOC(MSG_TOOT_CONTEXT_NEW),
        TAG_END);
    if (!tv->contextMessage) return FALSE;

    /* ------------------------------------------------------------------ */
    /* Main body editor - multi-line, word-wrapped, shares the font cache */
    /* ------------------------------------------------------------------ */
    tv->bodyEditor = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,                  (ULONG)GID_TOOT_BODY_EDITOR,
        ICA_TARGET,             (ULONG)TargetInstance,
        UTED_KeyMessageMode,    UKM_Internal,
        UTED_InternalRawKey_SendBack,TRUE,
        UTED_BevelStyle,        BVS_FIELD,
        UTED_URPDrawContext,    (ULONG)textDC,
        UTED_TextPen,           1UL,
        UTED_BgPen,             0UL,
        UTED_WordWrap,          TRUE,
        UTED_DisplayInternalVScroll, TRUE,
        UTED_LeftMargin,        2,
        UTED_TopMargin,         3,
        UTED_BottomMargin,      1,
        UTED_LineSpacing,       0,
        TAG_END);
    if (!tv->bodyEditor) return FALSE;

    /* ------------------------------------------------------------------ */
    /* Bottom bar: visibility chooser, char count, emoji buttons, Toot     */
    /* ------------------------------------------------------------------ */
    NewList(&tv->visibilityList);
    for (i = 0; i < FS3ETOOT_NUM_VISIBILITIES; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)LOC(visibilityMsgIds[i]), TAG_END);
        tv->visibilityNodes[i] = node;
        if (node) AddTail(&tv->visibilityList, node);
    }

    tv->visibilityChooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,          (ULONG)GID_TOOT_VISIBILITY,
        GA_RelVerify,   TRUE,
        ICA_TARGET,     (ULONG)TargetInstance,
        CHOOSER_PopUp,  TRUE,
        CHOOSER_Labels, (ULONG)&tv->visibilityList,
        CHOOSER_Active, 0UL,
        TAG_END);
    if (!tv->visibilityChooser) return FALSE;

    {
        char maxBuf[16];
        FormatMaxChars(app->accountMaxChars, maxBuf, sizeof(maxBuf));
        sprintf(tv->charCountText, LOC(MSG_TOOT_CHARS_FORMAT), 0UL, maxBuf);
    }
    tv->charCountLabel = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,          TRUE,
        BUTTON_BevelStyle,    BVS_NONE,
        BUTTON_Justification, BCJ_CENTER,
        GA_Text,              (ULONG)tv->charCountText,
        TAG_END);
    if (!tv->charCountLabel) return FALSE;

    tv->emojiBtn =
     NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,           GID_TOOT_EMOJI_BUTTON,
        GA_RelVerify,    TRUE,
        ICA_TARGET,      (ULONG)TargetInstance,
        UBT_BevelStyle,  BVS_BUTTON,
        UBT_URPDrawContext,    (ULONG)textDC,
        GA_Text,         (ULONG) "\xF0\x9F\x98\x8A",
        TAG_END);

    if (!tv->emojiBtn ) return FALSE;

    tv->tootBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_TOOT_SEND_BUTTON,
        GA_RelVerify, TRUE,
       // only use GADGETUP ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)LOC(MSG_TOOT_SEND),
        /* No account connected yet at all == nothing to post to. Refined
         * below by every FS3ETootView_UpdateSendEnabled() call once app's
         * account state can actually change (login/switch/load). */
        GA_Disabled,  (ULONG)(!(app->accountAccessToken && app->accountAccessToken[0])),
        TAG_END);
    if (!tv->tootBtn) return FALSE;

    bottomBar = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,     (ULONG)tv->visibilityChooser,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      100,
        LAYOUT_AddChild,     (ULONG)tv->charCountLabel,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      130,
        LAYOUT_AddChild,     (ULONG)tv->emojiBtn,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     (ULONG)Spacer(),
            CHILD_WeightedWidth, 1,
        LAYOUT_AddChild,     (ULONG)tv->tootBtn,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      80,
        TAG_END);

    /* ------------------------------------------------------------------ */
    /* Outer vertical layout                                              */
    /* ------------------------------------------------------------------ */
    tv->layout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_VERT,
                LAYOUT_SpaceOuter,  TRUE,
        LAYOUT_SpaceInner,  TRUE,
        LAYOUT_AddChild,    (ULONG)tv->contextMessage,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,    (ULONG)tv->bodyEditor,
            CHILD_WeightedHeight, 1,
        LAYOUT_AddChild,    (ULONG)bottomBar,
            CHILD_WeightedHeight, 0,

        TAG_END);
    if (!tv->layout) return FALSE;

    tv->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   100,
        WA_Top,    60,
        WA_Width,  420,
        WA_Height, 260,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE |
                   IDCMP_MENUPICK /*| IDCMP_RAWKEY*/,
        WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                   WFLG_SIZEGADGET | WFLG_SIZEBRIGHT | WFLG_SIZEBBOTTOM |
                   WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WA_Title,  (ULONG)LOC(MSG_TOOT_TITLE),
        WINDOW_ParentGroup, (ULONG)tv->layout,
        TAG_END);
    if (!tv->windowObj) {
        DisposeObject(tv->layout);
        tv->layout = NULL;
        return FALSE;
    }

    return TRUE;
}

void FS3ETootView_Dispose(FS3ETootView *tv)
{
    int i;

    if (!tv) return;

    if (tv->composePostId) {
        FreeVec(tv->composePostId);
        tv->composePostId = NULL;
    }
    for (i = 0; i < FS3ETOOT_MAX_MEDIA; i++) {
        if (tv->composeMediaIds[i]) {
            FreeVec(tv->composeMediaIds[i]);
            tv->composeMediaIds[i] = NULL;
        }
    }
    tv->composeMediaCount = 0;

    if (tv->windowObj) {
        FS3ETootView_Close(tv);
        DisposeObject(tv->windowObj);
        tv->windowObj = NULL;
    }

    if (ChooserBase) {
        for (i = 0; i < FS3ETOOT_NUM_VISIBILITIES; i++) {
            if (tv->visibilityNodes[i]) {
                FreeChooserNode(tv->visibilityNodes[i]);
                tv->visibilityNodes[i] = NULL;
            }
        }
    }
}

void FS3ETootView_Open(FS3ETootView *tv)
{
    if (!tv || !tv->windowObj) return;

    if (tv->window) {
        WindowToFront(tv->window);
        ActivateWindow(tv->window);
       DoMethod(tv->windowObj, WM_RETHINK, NULL);
        return; /* already open */
    }

    if (CurrentMainScreen) {
        SetAttrs(tv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    if (tv->width > 0) {
        SetAttrs(tv->windowObj,
                 WA_Left,   (ULONG)tv->left,
                 WA_Top,    (ULONG)tv->top,
                 WA_Width,  (ULONG)tv->width,
                 WA_Height, (ULONG)tv->height,
                 TAG_END);
    }
    if(tv->emojiBtn)
    {
        /* need to be font updated*/
        SetAttrs(tv->emojiBtn, GA_Text,(ULONG) "\xF0\x9F\x98\x8A",TAG_END);
    }

    tv->window = (struct Window *)DoMethod(tv->windowObj, WM_OPEN, NULL);

    /* Menu strips attach to the transient struct Window, not the
     * persistent BOOPSI Object * -- must be (re)built every open. */
    if (tv->window)
        FS3ETootView_MenuCreate(tv, tv->window->WScreen, tv->window);

    FS3ETootView_UpdateCharCount(tv);
    FS3ETootView_UpdateSendEnabled(tv);

    /* activate editor so it receive keyboard immediately */
    if(tv->window)
    {
        ActivateGadget(tv->bodyEditor,tv->window,NULL);
    }
    DoMethod(tv->windowObj, WM_RETHINK, NULL);
}

void FS3ETootView_Close(FS3ETootView *tv)
{
    /* closing the toot view closes the emojibox view */
    FS3EEmojiBoxWindow_Close(&app->emojiBoxWindow);

    if (!tv || !tv->windowObj || !tv->window) return;

    /* Detach the menu before the window itself goes away. */
    FS3ETootView_MenuClose(tv, tv->window);

    GetAttr(WA_Left,   tv->windowObj, (ULONG *)&tv->left);
    GetAttr(WA_Top,    tv->windowObj, (ULONG *)&tv->top);
    GetAttr(WA_Width,  tv->windowObj, (ULONG *)&tv->width);
    GetAttr(WA_Height, tv->windowObj, (ULONG *)&tv->height);

    DoMethod(tv->windowObj, WM_CLOSE, NULL);
    tv->window = NULL;
}


void FS3ETootView_ClearText(FS3ETootView *tv)
{
    if(!tv) return;
    if (tv->bodyEditor)
    {
        if(tv->window)
            SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                tv->window, NULL,
                UTED_SelectAll,       TRUE,
                UTED_DeleteSelection, TRUE,
                TAG_DONE);
        else
            SetAttrs((struct Gadget *)tv->bodyEditor,
                UTED_SelectAll,       TRUE,
                UTED_DeleteSelection, TRUE,
                TAG_DONE);
    }
    FS3ETootView_UpdateCharCount(tv);
}
BOOL FS3ETootView_HandleInput(FS3ETootView *tv)
{
    ULONG result;
    ULONG refreshFlags = 0;
    ULONG reactivateEditor=FALSE;
    if (!tv || !tv->windowObj) return FALSE;
    if (!tv->window) return TRUE; /* closed, that's fine */



    while ((result = DoMethod(tv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3ETootView_Close(tv);

                return TRUE;

            case WMHI_NEWSIZE:
                /* contextMessage is a UniButton, not an editor -- layout.gadget
                 * handles its resize like any other button (see
                 * fs3eloginview.c's urlInstructLabel, same reasoning). */
                if (tv->bodyEditor)
                    RefreshGList((struct Gadget *)tv->bodyEditor, tv->window, NULL, 1);
                break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;
                if (gadId == GID_TOOT_BODY_EDITOR)
                    FS3ETootView_UpdateCharCount(tv);

                BoopsiDelay_BeginMessage(DelayQueue, gadId);
                BoopsiDelay_AddTag(DelayQueue, GA_Selected, 0);
                BoopsiDelay_EndMessage(DelayQueue);
                break;
            }

            case WMHI_MENUPICK:
            {
                UWORD menuCode = (UWORD)(result & WMHI_MENUMASK);
                struct MenuItem *item;
                ULONG udata;

                while (menuCode != MENUNULL) {
                    item = ItemAddress(tv->menu, menuCode);
                    if (!item) break;
                    udata = (ULONG)GTMENUITEM_USERDATA(item);

                    switch ((FS3ETootMenuID)udata) {
                        case FS3ETMENU_CLEAR:
                            FS3ETootView_ClearText(tv);
                            break;

                        case FS3ETMENU_UNDO:
                            if (tv->bodyEditor)
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_Undo, TRUE, TAG_DONE);
                            FS3ETootView_UpdateCharCount(tv);
                            reactivateEditor = TRUE;
                            break;

                        case FS3ETMENU_REDO:
                            if (tv->bodyEditor)
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_Redo, TRUE, TAG_DONE);
                            FS3ETootView_UpdateCharCount(tv);
                            reactivateEditor = TRUE;
                            break;

                        case FS3ETMENU_CUT:
                            if (tv->bodyEditor)
                            {
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_ApplyCut, TRUE, TAG_DONE);
                                reactivateEditor = TRUE;
                            }
                            FS3ETootView_UpdateCharCount(tv);
                            reactivateEditor = TRUE;
                            break;

                        case FS3ETMENU_COPY:
                            if (tv->bodyEditor)
                            {
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_ApplyCopy, TRUE, TAG_DONE);

                                reactivateEditor = TRUE;
                            }
                            break;

                        case FS3ETMENU_PASTE:
                            if (tv->bodyEditor)
                            {
                                SetGadgetAttrs((struct Gadget *)tv->bodyEditor,
                                    tv->window, NULL,
                                    UTED_ApplyPaste, TRUE, TAG_DONE);

                                FS3ETootView_UpdateCharCount(tv);

                                reactivateEditor = TRUE;
                            }
                            break;

                        case FS3ETMENU_EMOJIBOX:
                            FS3EEmojiBoxWindow_Open(&app->emojiBoxWindow);
                            break;

                        default:
                            break; /* title item or unknown -- ignore */
                    }

                    menuCode = item->NextSelect;
                }
                break;
            }

           // case WMHI_RAWKEY:
           //  {
           //      ULONG key = (result & 0x07f);
           //      ULONG isUp = (result & 0x080);
           //      ULONG qualifiers=0;
           //      int keyUsed=0;

           //      GetAttr(WINDOW_Qualifier,app->window_obj,&qualifiers);
           //      if(!isUp && !keyUsed && tv->window)
           //      {
           //          keyUsed = (int) FS3EEmojiBox_HandleFKey(
           //            &app->emojiBoxWindow,
           //          tv->bodyEditor, key, qualifiers,tv->window);
           //      }
           //      // if (!keyUsed && app->activeEditorObj == app->textEditorObj)
           //      // {
           //      //     SetGdAttrs(app->textEditorObj,
           //      //         UTED_PutRawKey,(result & 0x0ff)|(qualifiers<<16),TAG_END);
           //      // }
           //  }
           //  break;

            default:
                break;
        }
    }
    if(reactivateEditor && tv->window && tv->bodyEditor)
    {
        ActivateGadget(tv->bodyEditor,tv->window,NULL);
    }

    return TRUE;
}

ULONG FS3ETootView_GetSignalMask(FS3ETootView *tv)
{
    if (!tv || !tv->window) return 0;
    return (1L << tv->window->UserPort->mp_SigBit);
}

void FS3ETootView_GetWindowPos(FS3ETootView *tv)
{
    if (!tv || !tv->windowObj || !tv->window) return;

    GetAttr(WA_Left,   tv->windowObj, (ULONG *)&tv->left);
    GetAttr(WA_Top,    tv->windowObj, (ULONG *)&tv->top);
    GetAttr(WA_Width,  tv->windowObj, (ULONG *)&tv->width);
    GetAttr(WA_Height, tv->windowObj, (ULONG *)&tv->height);
}

static const char *GetEditorUTF8Line(Object *editor, ULONG line)
{
    const char *text = NULL;

    if (!editor) return NULL;

    SetAttrs(editor, UTED_LineTextToGet, line, TAG_END);
    GetAttr(UTED_LineUTF8TextBuffer, editor, (ULONG *)&text);

    return text;
}

void FS3ETootView_UpdateCharCount(FS3ETootView *tv)
{
    ULONG lineCount = 0, i, total = 0;

    if (!tv || !tv->bodyEditor || !tv->charCountLabel) return;

    GetAttr(UTED_LineCount, tv->bodyEditor, &lineCount);
    for (i = 0; i < lineCount; i++) {
        const char *line = GetEditorUTF8Line(tv->bodyEditor, i);
        if (line) total += strlen(line);
        if (i + 1 < lineCount) total += 1; /* newline */
    }

    {
        char maxBuf[16];
        FormatMaxChars(app->accountMaxChars, maxBuf, sizeof(maxBuf));
        sprintf(tv->charCountText, LOC(MSG_TOOT_CHARS_FORMAT), (unsigned long)total, maxBuf);
    }
    if (tv->window)
        SetGadgetAttrs((struct Gadget *)tv->charCountLabel, tv->window, NULL,
                       GA_Text, (ULONG)tv->charCountText, TAG_END);
    else
        SetAttrs((Object *)tv->charCountLabel,
                 GA_Text, (ULONG)tv->charCountText, TAG_END);
}

void FS3ETootView_UpdateSendEnabled(FS3ETootView *tv)
{
    BOOL connected;

    if (!tv || !tv->tootBtn || !app) return;

    connected = (app->accountAccessToken && app->accountAccessToken[0]) ? TRUE : FALSE;

    if (tv->window)
        SetGadgetAttrs((struct Gadget *)tv->tootBtn, tv->window, NULL,
                       GA_Disabled, (ULONG)!connected, TAG_DONE);
    else
        SetAttrs(tv->tootBtn, GA_Disabled, (ULONG)!connected, TAG_END);
}

/* Everything that varies per FS3ETootKind, kept as one table instead of
 * scattered switch-statements -- adding a future kind (e.g. an actual poll
 * compose UI) means adding one row here, not touching three separate
 * places. FS3ETOOT_KIND_REPLY's titleMsgId is unused (its title is
 * printf-formatted with the replied-to @handle, not a plain lookup) --
 * handled as an explicit exception in FS3ETootView_SetComposeContext. */
typedef struct FS3ETootKindConfig {
    ULONG titleMsgId;         /* MSG_TOOT_CONTEXT_* for contextMessage's text */
    ULONG buttonMsgId;        /* MSG_TOOT_SEND_* for tootBtn's label */
    BOOL  prefillBody;        /* TRUE = bodyEditor is replaced with params->body */
    BOOL  visibilityEditable; /* FALSE disables visibilityChooser -- Mastodon's
                                * edit endpoint silently ignores visibility
                                * changes, so MODIFY shouldn't imply it works */
} FS3ETootKindConfig;

static const FS3ETootKindConfig tootKindConfig[] = {
    /* FS3ETOOT_KIND_NEW    */ { MSG_TOOT_CONTEXT_NEW,    MSG_TOOT_SEND,       FALSE, TRUE  },
    /* FS3ETOOT_KIND_MODIFY */ { MSG_TOOT_CONTEXT_MODIFY, MSG_TOOT_SEND_MODIFY, TRUE, FALSE },
    /* FS3ETOOT_KIND_POLL   */ { MSG_TOOT_CONTEXT_POLL,   MSG_TOOT_SEND,       FALSE, TRUE  },
    /* FS3ETOOT_KIND_REPLY  */ { MSG_TOOT_CONTEXT_NEW /* unused, see above */, MSG_TOOT_SEND_REPLY, TRUE, TRUE },
};

void FS3ETootView_SetComposeContext(FS3ETootView *tv, FS3ETootKind kind,
                                     const FS3ETootComposeParams *params)
{
    char textBuf[256];
    const char *text;
    const FS3ETootKindConfig *cfg;
    ULONG kindIdx = (ULONG)kind;

    if (!tv) return;

    if (kindIdx >= sizeof(tootKindConfig) / sizeof(tootKindConfig[0]))
        kindIdx = FS3ETOOT_KIND_NEW;
    cfg = &tootKindConfig[kindIdx];

    tv->composeKind = kind;

    if (tv->composePostId) {
        FreeVec(tv->composePostId);
        tv->composePostId = NULL;
    }
    if (params && params->postId && params->postId[0]) {
        ULONG n = (ULONG)strlen(params->postId) + 1;
        tv->composePostId = AllocVec(n, MEMF_ANY);
        if (tv->composePostId) CopyMem((APTR)params->postId, tv->composePostId, n);
    }

    {
        ULONG i;
        for (i = 0; i < FS3ETOOT_MAX_MEDIA; i++) {
            if (tv->composeMediaIds[i]) {
                FreeVec(tv->composeMediaIds[i]);
                tv->composeMediaIds[i] = NULL;
            }
        }
        tv->composeMediaCount = 0;
        if (params) {
            ULONG mc = params->mediaCount;
            if (mc > FS3ETOOT_MAX_MEDIA) mc = FS3ETOOT_MAX_MEDIA;
            for (i = 0; i < mc; i++) {
                if (params->mediaIds[i] && params->mediaIds[i][0]) {
                    ULONG n = (ULONG)strlen(params->mediaIds[i]) + 1;
                    tv->composeMediaIds[tv->composeMediaCount] = AllocVec(n, MEMF_ANY);
                    if (tv->composeMediaIds[tv->composeMediaCount]) {
                        CopyMem((APTR)params->mediaIds[i], tv->composeMediaIds[tv->composeMediaCount], n);
                        tv->composeMediaCount++;
                    }
                }
            }
        }
    }

    if( kind == FS3ETOOT_KIND_NEW )
    {
        FS3ETootView_ClearText(tv);
    }

    if (kind == FS3ETOOT_KIND_REPLY) {
        snprintf(textBuf, sizeof(textBuf)-1, LOC(MSG_TOOT_CONTEXT_REPLY_FORMAT),
                 (params && params->acct && params->acct[0]) ? params->acct : "?");
        text = textBuf;
    } else {
        text = LOC(cfg->titleMsgId);
    }

    if (tv->contextMessage) {
        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->contextMessage, tv->window, NULL,
                           GA_Text, (ULONG)text, TAG_DONE);
        else
            SetAttrs(tv->contextMessage, GA_Text, (ULONG)text, TAG_END);
    }

    if (tv->tootBtn) {
        const char *btnText = LOC(cfg->buttonMsgId);

        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->tootBtn, tv->window, NULL,
                           GA_Text, (ULONG)btnText, TAG_DONE);
        else
            SetAttrs(tv->tootBtn, GA_Text, (ULONG)btnText, TAG_END);
    }

    if (tv->visibilityChooser) {
        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->visibilityChooser, tv->window, NULL,
                           GA_Disabled, (ULONG)!cfg->visibilityEditable, TAG_DONE);
        else
            SetAttrs(tv->visibilityChooser, GA_Disabled, (ULONG)!cfg->visibilityEditable, TAG_END);
    }

    /* Prefill the body -- only for kinds that declare it, every other kind
     * starts from an empty editor. MODIFY prefills with what's already
     * posted (params->body); REPLY prefills with an "@acct " mention
     * prefix built from params->acct, the standard "who this reply is
     * addressed to" convention every mainstream Mastodon client shows. */
    if (cfg->prefillBody && tv->bodyEditor) {
        char bodyBuf[300];
        const char *body;

        if (kind == FS3ETOOT_KIND_REPLY) {
            snprintf(bodyBuf, sizeof(bodyBuf), "@%s ",
                     (params && params->acct && params->acct[0]) ? params->acct : "");
            body = bodyBuf;
        } else {
            body = (params && params->body) ? params->body : "";
        }

        if (tv->window)
            SetGadgetAttrs((struct Gadget *)tv->bodyEditor, tv->window, NULL,
                           UTED_Text, (ULONG)body, TAG_DONE);
        else
            SetAttrs(tv->bodyEditor, UTED_Text, (ULONG)body, TAG_END);

        FS3ETootView_UpdateCharCount(tv);
    }
}

/* WATCH OUT ! if return not NULL , must be FreeVec()'ed */
const char *FS3ETootView_GetUTF8Body(FS3ETootView *tv)
{
    if (!tv) return NULL;
    const char *p=NULL;
    if(tv->bodyEditor)
    {
        GetAttr(UTED_Text,tv->bodyEditor,(ULONG)&p);
    }
    return p;

    //return GetEditorUTF8Line(tv->bodyEditor, 0);
}

LONG FS3ETootView_GetVisibility(FS3ETootView *tv)
{
    ULONG active = 0;

    if (!tv || !tv->visibilityChooser) return 0;

    GetAttr(CHOOSER_Active, tv->visibilityChooser, &active);
    return (LONG)active;
}
