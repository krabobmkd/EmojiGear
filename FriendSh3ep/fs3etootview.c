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

#include <intuition/icclass.h>

#include "fs3etootview.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3egadgetid.h"
#include "fs3elocale.h"

extern struct Library *ChooserBase;

static const ULONG visibilityMsgIds[FS3ETOOT_NUM_VISIBILITIES] = {
    MSG_TOOT_VISIBILITY_PUBLIC, MSG_TOOT_VISIBILITY_UNLISTED,
    MSG_TOOT_VISIBILITY_PRIVATE, MSG_TOOT_VISIBILITY_DIRECT
};

/* A transparent, read-only button used as a layout spacer. */
static Object *Spacer(void)
{
    return (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,        TRUE,
        BUTTON_BevelStyle,  BVS_NONE,
        BUTTON_Transparent, TRUE,
        TAG_END);
}

static Object *MakeEmojiButton(ULONG gadId, const char *glyph)
{
    return (Object *)NewObject(UNIBUTTON_GetClass(), NULL,
        GA_ID,           gadId,
        GA_RelVerify,    TRUE,
        ICA_TARGET,      (ULONG)TargetInstance,
        UBT_BevelStyle,  BVS_BUTTON,
        UBT_AddFont,     (ULONG)"NotoColorEmoji32.ttf",
        GA_Text,         (ULONG)glyph,
        TAG_END);
}

BOOL FS3ETootView_Create(FS3ETootView *tv, ULONG pointSize)
{
    Object *subjectLabel;
    Object *subjectRow, *bottomBar;
    ULONG sharedDc = 0;
    int i;

    {
        LONG sl = tv->left, st = tv->top, sw = tv->width, sh = tv->height;
        memset(tv, 0, sizeof(*tv));
        tv->left = sl; tv->top = st; tv->width = sw; tv->height = sh;
    }

    /* ------------------------------------------------------------------ */
    /* "CW or subject" - single line editor                               */
    /* ------------------------------------------------------------------ */
    tv->subjectEditor = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,                  (ULONG)GID_TOOT_SUBJECT_EDITOR,
        ICA_TARGET,             (ULONG)TargetInstance,
        UTED_KeyMessageMode,    UKM_Internal,
        UTED_BevelStyle,        BVS_FIELD,
        UTED_PointSize,         pointSize,
        UTED_AddFont,           (ULONG)"LiberationSans-Regular.ttf",
        UTED_TextPen,           1UL,
        UTED_BgPen,             0UL,
        UTED_MaxDisplayLines,   1UL,
        UTED_NoLineFeed,        TRUE,
        UTED_WordWrap,          FALSE,
        UTED_LeftMargin,        2,
        UTED_TopMargin,         3,
        UTED_BottomMargin,      1,
        UTED_LineSpacing,       0,
        TAG_END);
    if (!tv->subjectEditor) return FALSE;

    GetAttr(UTED_URPDrawContext, tv->subjectEditor, &sharedDc);

    /* ------------------------------------------------------------------ */
    /* Main body editor - multi-line, word-wrapped, shares the font cache */
    /* ------------------------------------------------------------------ */
    tv->bodyEditor = (Object *)NewObject(UNITEXTEDITOR_GetClass(), NULL,
        GA_ID,                  (ULONG)GID_TOOT_BODY_EDITOR,
        ICA_TARGET,             (ULONG)TargetInstance,
        UTED_KeyMessageMode,    UKM_Internal,
        UTED_BevelStyle,        BVS_FIELD,
        UTED_PointSize,         pointSize,
        UTED_URPDrawContext,    (ULONG)sharedDc,
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

    subjectLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_TOOT_SUBJECT),
        TAG_END);

    subjectRow = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation, LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,    (ULONG)tv->subjectEditor,
            CHILD_WeightedWidth, 1,
            CHILD_Label,         (ULONG)subjectLabel,
        TAG_END);

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

    sprintf(tv->charCountText, LOC(MSG_TOOT_CHARS_FORMAT), 0UL);
    tv->charCountLabel = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ReadOnly,          TRUE,
        BUTTON_BevelStyle,    BVS_NONE,
        BUTTON_Justification, BCJ_CENTER,
        GA_Text,              (ULONG)tv->charCountText,
        TAG_END);
    if (!tv->charCountLabel) return FALSE;

    tv->emojiBtn1 = MakeEmojiButton(GID_TOOT_EMOJI1_BUTTON, "\xF0\x9F\x98\x80" /* grinning face */);
    tv->emojiBtn2 = MakeEmojiButton(GID_TOOT_EMOJI2_BUTTON, "\xF0\x9F\x98\x8A" /* smiling face */);
    if (!tv->emojiBtn1 || !tv->emojiBtn2) return FALSE;

    tv->tootBtn = (Object *)NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        (ULONG)GID_TOOT_SEND_BUTTON,
        GA_RelVerify, TRUE,
        ICA_TARGET,   (ULONG)TargetInstance,
        GA_Text,      (ULONG)LOC(MSG_TOOT_SEND),
        TAG_END);
    if (!tv->tootBtn) return FALSE;

    bottomBar = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_AddChild,     (ULONG)tv->visibilityChooser,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      100,
        LAYOUT_AddChild,     (ULONG)tv->charCountLabel,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth,      80,
        LAYOUT_AddChild,     (ULONG)tv->emojiBtn1,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild,     (ULONG)tv->emojiBtn2,
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
        LAYOUT_AddChild,    (ULONG)subjectRow,
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
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE,
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

    tv->window = (struct Window *)DoMethod(tv->windowObj, WM_OPEN, NULL);

    FS3ETootView_UpdateCharCount(tv);
}

void FS3ETootView_Close(FS3ETootView *tv)
{
    if (!tv || !tv->windowObj || !tv->window) return;

    GetAttr(WA_Left,   tv->windowObj, (ULONG *)&tv->left);
    GetAttr(WA_Top,    tv->windowObj, (ULONG *)&tv->top);
    GetAttr(WA_Width,  tv->windowObj, (ULONG *)&tv->width);
    GetAttr(WA_Height, tv->windowObj, (ULONG *)&tv->height);

    DoMethod(tv->windowObj, WM_CLOSE, NULL);
    tv->window = NULL;
}

BOOL FS3ETootView_HandleInput(FS3ETootView *tv)
{
    ULONG result;
    ULONG refreshFlags = 0;
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
                if (tv->subjectEditor)
                    RefreshGList((struct Gadget *)tv->subjectEditor, tv->window, NULL, 1);
                if (tv->bodyEditor)
                    RefreshGList((struct Gadget *)tv->bodyEditor, tv->window, NULL, 1);
                break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;

                if (gadId == GID_TOOT_BODY_EDITOR || gadId == GID_TOOT_SUBJECT_EDITOR)
                    FS3ETootView_UpdateCharCount(tv);

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

ULONG FS3ETootView_GetSignalMask(FS3ETootView *tv)
{
    if (!tv || !tv->window) return 0;
    return (1L << tv->window->UserPort->mp_SigBit);
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

    sprintf(tv->charCountText, LOC(MSG_TOOT_CHARS_FORMAT), (unsigned long)total);
    if (tv->window)
        SetGadgetAttrs((struct Gadget *)tv->charCountLabel, tv->window, NULL,
                       GA_Text, (ULONG)tv->charCountText, TAG_END);
    else
        SetAttrs((Object *)tv->charCountLabel,
                 GA_Text, (ULONG)tv->charCountText, TAG_END);
}

const char *FS3ETootView_GetUTF8Subject(FS3ETootView *tv)
{
    if (!tv) return NULL;
    return GetEditorUTF8Line(tv->subjectEditor, 0);
}

const char *FS3ETootView_GetUTF8Body(FS3ETootView *tv)
{
    if (!tv) return NULL;
    return GetEditorUTF8Line(tv->bodyEditor, 0);
}

LONG FS3ETootView_GetVisibility(FS3ETootView *tv)
{
    ULONG active = 0;

    if (!tv || !tv->visibilityChooser) return 0;

    GetAttr(CHOOSER_Active, tv->visibilityChooser, &active);
    return (LONG)active;
}
