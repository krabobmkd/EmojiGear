/*
 * BoopsiMessage - Delayed BOOPSI notification queue for Petmate Amiga.
 * Adapted from aukboopsi/boopsimessage.c.
 *
 * A private unnamed modelclass is created. One instance of it is the
 * ICA_TARGET for all gadgets. The dispatcher queues OM_NOTIFY attributes
 * we care about and signals the main task to drain the queue later.
 */

#include "boopsimessage.h"
#include "bdbprintf.h"
#include "compilers.h"

#include <string.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/utility.h>
/* for CTRL_F */
#include <dos/dos.h>

#include <intuition/gadgetclass.h>
#include <gadgets/button.h>

#include <gadgets/unitexteditor.h>


/* Maximum tag entries in the queue */
#define BOOPSIDELAY_QUEUE_SIZE 256

/* Queue structure - instance data of TargetModelClass */
struct BoopsiDelayQueue {
    struct TagItem queue[BOOPSIDELAY_QUEUE_SIZE];
    UWORD writePos;
    UWORD readPos;
    BOOL  hasMessages;
};

/* Global pointers */
static Class *TargetModelClass = NULL;
Object       *TargetInstance   = NULL;
BoopsiDelayQueue *DelayQueue   = NULL;

/* myTask is declared in petmate.c */
extern struct Task *myTask;

/* Attributes we delay from OM_NOTIFY.
 * Phase 2: only GA_Selected (tool buttons).
 * Later phases will add PETSCIICANVAS_*, CHARSELECTOR_*, etc. */
static ULONG delayedAttribs[] = {
    GA_Selected,

    /* for border scrollers */
    PGA_Top,
    PGA_Total,
    PGA_Visible,

    UTEDN_CursorMoved,
    UTEDN_TextChanged,
    UTEDN_ScrollChanged,
    UTEDN_UndoAvailable,
    UTEDN_RedoAvailable,

    UTED_AddFont,
    UTED_Modified,
    UTED_CursorLine,
    UTED_CursorChar,
    UTED_ScrollLeft,
    UTED_ScrollTop,

    UTED_SetPrivateActivation,


};

#define NB_DELAYED_ATTRIBS ((ULONG)(sizeof(delayedAttribs) / sizeof(ULONG)))

/* Union covering all BOOPSI message types */
typedef union {
    ULONG              MethodID;
    struct opSet       opSet;
    struct opUpdate    opUpdate;
    struct opGet       opGet;
    struct gpRender    gpRender;
    struct gpInput     gpInput;
    struct gpLayout    gpLayout;
} *Msgs;

typedef ULONG (*REHOOKFUNC)();

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   Private modelclass dispatcher
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */
static ULONG ASM SAVEDS TargetModelDispatch(
    REG(a0, struct IClass *C),
    REG(a2, Object *obj),
    REG(a1, union { ULONG MethodID; struct opUpdate opUpdate; } *M))
{
    ULONG retval = 0;
    ULONG i;

    switch (M->MethodID) {
        case OM_NEW:
            obj = (Object *)DoSuperMethodA(C, obj, (Msg)M);
            if (obj) {
                DelayQueue = (BoopsiDelayQueue *)INST_DATA(C, obj);
                memset(DelayQueue, 0, sizeof(BoopsiDelayQueue));
                DelayQueue->writePos   = 0;
                DelayQueue->readPos    = 0;
                DelayQueue->hasMessages = FALSE;
                retval = (ULONG)obj;
            }
            break;

        case OM_DISPOSE:
            retval = DoSuperMethodA(C, obj, (Msg)M);
            DelayQueue = NULL;
            break;

        case OM_NOTIFY:
        case OM_UPDATE:
        {
            struct TagItem *ptag;
            ULONG sender_ID = 0;

            /* Extract the sender's GA_ID */
            ptag = FindTagItem(GA_ID, M->opUpdate.opu_AttrList);
            if (ptag) sender_ID = ptag->ti_Data;

            if (sender_ID != 0) {
                BoopsiDelay_BeginMessage(DelayQueue, sender_ID);

                for (i = 0; i < NB_DELAYED_ATTRIBS; i++) {
                    ptag = FindTagItem(delayedAttribs[i],
                                       M->opUpdate.opu_AttrList);
                    if (ptag) {
                        BoopsiDelay_AddTag(DelayQueue,
                                           delayedAttribs[i],
                                           ptag->ti_Data);
                    }
                }

                BoopsiDelay_EndMessage(DelayQueue);

                /* Signal main loop to process queue */
                if (myTask) Signal(myTask, SIGBREAKF_CTRL_F);

                retval = 1;
            }
            break;
        }

        default:
            retval = DoSuperMethodA(C, obj, (Msg)M);
            break;
    }
    return retval;
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   Public init/close
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

int initMessageTargetModel(void)
{
    TargetModelClass = MakeClass(NULL, "modelclass", NULL,
                                 sizeof(BoopsiDelayQueue), 0);
    if (!TargetModelClass) return 0;
    bdbprintf_makeclass("TargetModelClass", TargetModelClass);

    TargetModelClass->cl_Dispatcher.h_Entry =
        (REHOOKFUNC)&TargetModelDispatch;

    TargetInstance = (Object *)NewObject(TargetModelClass, NULL, TAG_DONE);
    if (!TargetInstance) return 0;

    return 1;
}

void closeMessageTargetModel(void)
{
    if (TargetInstance) {
        DisposeObject(TargetInstance);
        TargetInstance = NULL;
    }
    if (TargetModelClass) {
        bdbprintf_freeclass("TargetModelClass", TargetModelClass);
        FreeClass(TargetModelClass);
        TargetModelClass = NULL;
    }
}

/* - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
   Queue operations
   - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - */

BOOL BoopsiDelay_AddTag(BoopsiDelayQueue *q, ULONG tag, ULONG data)
{
    UWORD maxPos;

    if (!q) return FALSE;

    /* Reserve last 4 slots for TAG_END */
    maxPos = (tag == TAG_END) ? BOOPSIDELAY_QUEUE_SIZE
                              : BOOPSIDELAY_QUEUE_SIZE - 4;

    if (q->writePos >= maxPos) return FALSE;

    q->queue[q->writePos].ti_Tag  = tag;
    q->queue[q->writePos].ti_Data = data;
    q->writePos++;
    return TRUE;
}

BOOL BoopsiDelay_BeginMessage(BoopsiDelayQueue *q, ULONG senderID)
{
    if (!q) return FALSE;
    /* Need at least 2 slots: GA_ID entry + TAG_END */
    if (q->writePos >= BOOPSIDELAY_QUEUE_SIZE - 1) return FALSE;
    return BoopsiDelay_AddTag(q, GA_ID, senderID);
}

BOOL BoopsiDelay_EndMessage(BoopsiDelayQueue *q)
{
    if (!q) return FALSE;
    if (!BoopsiDelay_AddTag(q, TAG_END, 0)) return FALSE;
    q->hasMessages = TRUE;
    return TRUE;
}

BOOL BoopsiDelay_HasMessages(BoopsiDelayQueue *q)
{
    if (!q) return FALSE;
    return q->hasMessages;
}

struct TagItem *BoopsiDelay_NextMessage(BoopsiDelayQueue *q)
{
    struct TagItem *msg;

    if (!q || !q->hasMessages || q->readPos >= q->writePos) {
        if (q) {
            q->readPos    = 0;
            q->writePos   = 0;
            q->hasMessages = FALSE;
        }
        return NULL;
    }

    msg = &q->queue[q->readPos];

    /* Advance readPos past the TAG_END of this message */
    while (q->readPos < q->writePos) {
        if (q->queue[q->readPos].ti_Tag == TAG_END) {
            q->readPos++;
            break;
        }
        q->readPos++;
    }

    /* Check if more messages remain */
    if (q->readPos >= q->writePos) {
        q->readPos    = 0;
        q->writePos   = 0;
        q->hasMessages = FALSE;
    }

    return msg;
}
