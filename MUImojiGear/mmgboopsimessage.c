/*
 * mmgboopsimessage.c – Delayed BOOPSI notification queue for MUImojiGear.
 *
 * Adapted from EmojiGear/boopsimessage.c:
 *   - Uses REGARG() calling convention (no compilers.h dependency)
 *   - References mmgMainTask (declared in muimojiGear.c) instead of myTask
 *   - No bdbprintf debug calls
 *   - Trimmed delayed-attributes list: PGA_border-scroller tags removed
 *     (MUImojiGear uses UTED_DisplayInternalVScroll, not external prop gadgets)
 */

#include "mmgboopsimessage.h"
#include "mmgemojibox.h"    /* EGRID_ClickedIdx, EGRID_ClickSeq */

#include <string.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/utility.h>
#include <dos/dos.h>              /* SIGBREAKF_CTRL_F */
#include <intuition/gadgetclass.h>
#include <gadgets/unitexteditor.h>

/* --------------------------------------------------------------------------
 * Register calling convention – same helper used in testMUIUniTextEditor.c
 * -------------------------------------------------------------------------- */
#ifdef __GNUC__
#  define REGARG(reg, decl) decl __asm(#reg)
#else
#  define REGARG(reg, decl) register __##reg decl
#endif

/* Main task to wake when a notification arrives – set by MmgBoopsi_Init(). */
extern struct Task *mmgMainTask;

/* --------------------------------------------------------------------------
 * Queue dimensions
 * -------------------------------------------------------------------------- */
#define MMG_QUEUE_SIZE  256

struct MmgDelayQueue {
    struct TagItem queue[MMG_QUEUE_SIZE];
    UWORD writePos;
    UWORD readPos;
    BOOL  hasMessages;
};

/* --------------------------------------------------------------------------
 * Globals
 * -------------------------------------------------------------------------- */
static Class          *mmgTargetClass    = NULL;
Object                *MmgTargetInstance = NULL;
MmgDelayQueue         *MmgQueue          = NULL;

/* --------------------------------------------------------------------------
 * Attributes to relay from OM_NOTIFY into the queue.
 * Only the ones MUImojiGear actually consumes are listed.
 * -------------------------------------------------------------------------- */
static const ULONG delayedAttribs[] = {
    UTEDN_CursorMoved,
    UTEDN_TextChanged,
    UTEDN_ScrollChanged,
    UTEDN_UndoAvailable,
    UTEDN_RedoAvailable,
    UTED_Modified,
    UTED_CursorLine,
    UTED_CursorChar,
    UTED_ScrollLeft,      /* horizontal scroll position changed */
    UTED_SetPrivateActivation,
    UTED_InternalRawKey_Code,
    GA_Selected,          /* UniButton press notification */
    EGRID_ClickedIdx,     /* EmojiGrid cell click         */
    EGRID_ClickSeq,       /* EmojiGrid click counter      */
};

#define N_DELAYED ((ULONG)(sizeof(delayedAttribs) / sizeof(delayedAttribs[0])))

/* --------------------------------------------------------------------------
 * Dispatcher (runs in input.device context – only Signal/FindTagItem safe)
 * -------------------------------------------------------------------------- */
static ULONG notifyDispatch(
    REGARG(a0, Class  *cl),
    REGARG(a2, Object *obj),
    REGARG(a1, Msg     msg))
{
    switch (msg->MethodID) {

    case OM_NEW: {
        Object *self = (Object *)DoSuperMethodA(cl, obj, msg);
        if (self) {
            MmgQueue = (MmgDelayQueue *)INST_DATA(cl, self);
            memset(MmgQueue, 0, sizeof(MmgDelayQueue));
        }
        return (ULONG)self;
    }

    case OM_DISPOSE:
        MmgQueue = NULL;
        return DoSuperMethodA(cl, obj, msg);

    case OM_NOTIFY:
    case OM_UPDATE: {
        struct opUpdate *upd = (struct opUpdate *)msg;
        struct TagItem  *tag;
        ULONG            senderID = 0;
        ULONG            i;
        MmgDelayQueue   *q = MmgQueue;

        if (!q) return 0;

        tag = FindTagItem(GA_ID, upd->opu_AttrList);
        if (tag) senderID = (ULONG)tag->ti_Data;
        if (!senderID) return 0;

        /* Write message header: GA_ID, senderID */
        if (q->writePos >= MMG_QUEUE_SIZE - 2) return 0; /* no room */
        q->queue[q->writePos].ti_Tag  = GA_ID;
        q->queue[q->writePos].ti_Data = senderID;
        q->writePos++;

        /* Write interesting attribute/value pairs */
        for (i = 0; i < N_DELAYED; i++) {
            tag = FindTagItem(delayedAttribs[i], upd->opu_AttrList);
            if (tag && q->writePos < MMG_QUEUE_SIZE - 1) {
                q->queue[q->writePos].ti_Tag  = delayedAttribs[i];
                q->queue[q->writePos].ti_Data = (ULONG)tag->ti_Data;
                q->writePos++;
            }
        }

        /* Write message terminator */
        q->queue[q->writePos].ti_Tag  = TAG_END;
        q->queue[q->writePos].ti_Data = 0;
        q->writePos++;
        q->hasMessages = TRUE;

        if (mmgMainTask) Signal(mmgMainTask, SIGBREAKF_CTRL_F);
        return 1;
    }

    default:
        return DoSuperMethodA(cl, obj, msg);
    }
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

int MmgBoopsi_Init(void)
{
    mmgTargetClass = MakeClass(NULL, "modelclass", NULL,
                               sizeof(MmgDelayQueue), 0);
    if (!mmgTargetClass) return 0;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
    mmgTargetClass->cl_Dispatcher.h_Entry = (HOOKFUNC)notifyDispatch;
#pragma GCC diagnostic pop

    MmgTargetInstance = (Object *)NewObject(mmgTargetClass, NULL, TAG_DONE);
    if (!MmgTargetInstance) {
        FreeClass(mmgTargetClass);
        mmgTargetClass = NULL;
        return 0;
    }
    return 1;
}

void MmgBoopsi_Close(void)
{
    if (MmgTargetInstance) {
        DisposeObject(MmgTargetInstance);
        MmgTargetInstance = NULL;
    }
    if (mmgTargetClass) {
        FreeClass(mmgTargetClass);
        mmgTargetClass = NULL;
    }
    MmgQueue = NULL;
}

BOOL MmgBoopsi_HasMessages(void)
{
    return (MmgQueue && MmgQueue->hasMessages) ? TRUE : FALSE;
}

struct TagItem *MmgBoopsi_NextMessage(void)
{
    MmgDelayQueue  *q = MmgQueue;
    struct TagItem *msg;

    if (!q || !q->hasMessages || q->readPos >= q->writePos) {
        if (q) { q->readPos = 0; q->writePos = 0; q->hasMessages = FALSE; }
        return NULL;
    }

    msg = &q->queue[q->readPos];

    /* Advance past this record's TAG_END */
    while (q->readPos < q->writePos) {
        if (q->queue[q->readPos].ti_Tag == TAG_END) { q->readPos++; break; }
        q->readPos++;
    }

    if (q->readPos >= q->writePos) {
        q->readPos = 0; q->writePos = 0; q->hasMessages = FALSE;
    }

    return msg;
}
