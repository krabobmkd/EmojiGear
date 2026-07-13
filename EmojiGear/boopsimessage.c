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
#include <exec/semaphores.h>
/* for CTRL_F */
#include <dos/dos.h>

#include <intuition/gadgetclass.h>
#include <gadgets/button.h>

#include <gadgets/unitexteditor.h>
#include <gadgets/clicktab.h>

/* Maximum tag entries in the queue */
#define BOOPSIDELAY_QUEUE_SIZE 256

/*
 * Double-buffered queue - instance data of TargetModelClass.
 *
 * Why double-buffered rather than one locked queue: a gadget's OM_NOTIFY/
 * OM_UPDATE can happen from any task, at any time, including recursively
 * while the main loop is busy processing a just-drained message (that
 * processing can itself trigger another DoMethod(target, OM_UPDATE/
 * OM_NOTIFY)). A single locked queue would force every writer to wait out
 * however long that processing takes -- exactly backwards for a UI event
 * queue, where "append a new event" must never block on "handle the
 * previous one".
 *
 * So: two physical buffers (buf[0], buf[1]). Writers always append to
 * buf[active] -- BeginMessage/AddTag/EndMessage hold sem for that whole
 * sequence (one logical message must land in one buffer, not be split
 * across a concurrent swap), but that critical section is a handful of
 * memory writes, never anything that blocks or calls back out.
 *
 * BoopsiDelay_HasMessages(), called once per main-loop iteration right
 * before draining, holds sem only long enough to flip `active` -- buf[the
 * old active] becomes the drain buffer (writers can no longer reach it),
 * and buf[the new active] (== the previous drain buffer, guaranteed
 * fully consumed by then, see the assertion-like check below) is reset
 * for writers to reuse. drainLimit snapshots how many entries are in the
 * drain buffer at swap time.
 *
 * BoopsiDelay_NextMessage() then walks the drain buffer with NO locking
 * at all: nothing writes to it anymore until the next swap, and the next
 * swap only happens after this drain fully exhausts it (readPos reaches
 * drainLimit) -- so message *processing*, however slow or re-entrant, is
 * completely decoupled from writer latency.
 */
struct BoopsiDelayBuf {
    struct TagItem queue[BOOPSIDELAY_QUEUE_SIZE];
    UWORD writePos;
};

struct BoopsiDelayQueue {
    struct BoopsiDelayBuf  buf[2];
    UBYTE  active;      /* index writers currently append to; drain buffer is buf[1-active] */
    UWORD  readPos;     /* reader's cursor into buf[1-active] -- reader-owned, no lock needed */
    UWORD  drainLimit;  /* snapshot of buf[1-active].writePos taken at the last swap */
    struct SignalSemaphore sem;
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

    /* from border scrollers */
    PGA_Top,
    PGA_Total,
    PGA_Visible,

    /* from clicktab to send which was closed */
    CLICKTAB_NodeClosed,

    /* from UniTextEditor gadget */
    UTEDN_CursorMoved,
    UTEDN_TextChanged,
    UTEDN_ScrollChanged,
    UTEDN_UndoAvailable,
    UTEDN_RedoAvailable,

    UTED_AddFont,
    UTED_Modified,
    UTED_CursorLine,
    UTED_CursorColumn,
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
                InitSemaphore(&DelayQueue->sem);
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
   /* usefull routine to trace incoming messages */
#ifdef TRACE_ICOMING_MESSAGES
    bdbprintf("mess\n");
  struct TagItem *ptagb=M->opUpdate.opu_AttrList;
  while(ptagb->ti_Tag != NULL)
  {
    bdbprintf("t:%08x d:%08x\n",(int)ptagb->ti_Tag,ptagb->ti_Data);
    ptagb++;
  }
#endif

                /* BeginMessage + AddTag (repeated) + EndMessage is one
                 * logical message -- must be atomic as a whole against a
                 * concurrent swap on the main task (see struct
                 * BoopsiDelayQueue's comment), not just individually
                 * locked per call. */
                ObtainSemaphore(&DelayQueue->sem);

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

                ReleaseSemaphore(&DelayQueue->sem);

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
    struct BoopsiDelayBuf *b;
    UWORD maxPos;

    if (!q) return FALSE;
    b = &q->buf[q->active];

    /* Reserve last 4 slots for TAG_END */
    maxPos = (tag == TAG_END) ? BOOPSIDELAY_QUEUE_SIZE
                              : BOOPSIDELAY_QUEUE_SIZE - 4;

    if (b->writePos >= maxPos) return FALSE;

    b->queue[b->writePos].ti_Tag  = tag;
    b->queue[b->writePos].ti_Data = data;
    b->writePos++;
    return TRUE;
}

BOOL BoopsiDelay_BeginMessage(BoopsiDelayQueue *q, ULONG senderID)
{
    if (!q) return FALSE;
    /* Need at least 2 slots: GA_ID entry + TAG_END */
    if (q->buf[q->active].writePos >= BOOPSIDELAY_QUEUE_SIZE - 1) return FALSE;
    return BoopsiDelay_AddTag(q, GA_ID, senderID);
}

BOOL BoopsiDelay_EndMessage(BoopsiDelayQueue *q)
{
    if (!q) return FALSE;
    return BoopsiDelay_AddTag(q, TAG_END, 0);
}

/*
 * Swaps the double buffer: called once per main-loop iteration, right
 * before draining. buf[1-active] (the current drain buffer) only becomes
 * the new write target once this confirms it has already been fully
 * consumed (readPos caught up to the drainLimit snapshot from the last
 * swap) -- true by construction given the caller's use pattern (fully
 * drains via BoopsiDelay_NextMessage before calling this again), checked
 * defensively rather than assumed: if somehow not, this reports whatever
 * remains in the *current* drain buffer without swapping, so nothing is
 * ever skipped or handed to a writer while still unread.
 */
BOOL BoopsiDelay_HasMessages(BoopsiDelayQueue *q)
{
    BOOL result;
    if (!q) return FALSE;

    ObtainSemaphore(&q->sem);

    if (q->readPos >= q->drainLimit) {
        UBYTE oldActive = q->active;
        q->active     = (UBYTE)(1 - oldActive);
        q->drainLimit  = q->buf[oldActive].writePos;
        q->readPos     = 0;
        q->buf[q->active].writePos = 0; /* flush the new write target */
    }

    result = (q->readPos < q->drainLimit);

    ReleaseSemaphore(&q->sem);
    return result;
}

/*
 * Copies the message out to a caller-owned buffer rather than returning a
 * pointer straight into the drain buffer -- keeps BoopsiDelay_NextMessage()
 * itself lock-free (see struct BoopsiDelayQueue's comment: nothing writes
 * to buf[1-active] between swaps, so no lock is needed to read it), while
 * still giving the caller a stable snapshot instead of a pointer that
 * would become stale the moment the *next* swap flushes this same slot
 * for reuse. A message is at most NB_DELAYED_ATTRIBS+2 entries (GA_ID +
 * every delayed attribute + TAG_END); BOOPSIDELAY_MSG_MAX comfortably
 * covers that with room to grow.
 */
#define BOOPSIDELAY_MSG_MAX 32

struct TagItem *BoopsiDelay_NextMessage(BoopsiDelayQueue *q)
{
    static struct TagItem msgBuf[BOOPSIDELAY_MSG_MAX];
    struct BoopsiDelayBuf *drainBuf;
    UWORD n = 0;

    if (!q || q->readPos >= q->drainLimit) return NULL;

    drainBuf = &q->buf[1 - q->active];

    while (q->readPos < q->drainLimit && n < BOOPSIDELAY_MSG_MAX - 1) {
        BOOL isEnd = (drainBuf->queue[q->readPos].ti_Tag == TAG_END);
        msgBuf[n] = drainBuf->queue[q->readPos];
        q->readPos++;
        n++;
        if (isEnd) break;
    }
    if (n == 0 || msgBuf[n - 1].ti_Tag != TAG_END) {
        msgBuf[n].ti_Tag  = TAG_END;
        msgBuf[n].ti_Data = 0;
        n++;
    }

    return msgBuf;
}
