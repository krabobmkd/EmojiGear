/*
 * fs3etimer.c - VBlank-driven periodic timer service for FriendSh3ep.
 *
 * See fs3etimer.h for the public API and usage patterns. Implementation
 * notes on interrupt safety, since getting this wrong hangs or crashes
 * the whole machine, not just this app:
 *
 * - FS3ETimer_IntServer runs in genuine hardware interrupt context (added
 *   to the INTB_VERTB server chain via AddIntServer -- exec itself owns
 *   the actual VBlank interrupt and calls every registered server once
 *   per vertical blank, unconditionally, no ack/chaining return value
 *   needed unlike a raw INTB_PORTS-style handler). From there it is only
 *   ever safe to touch plain data (increment a counter, set a few BOOL
 *   flags) and call the handful of Exec functions explicitly documented
 *   as interrupt-safe -- Signal() is one of them, which is the only Exec
 *   call this interrupt makes. Everything else (calling a caller-supplied
 *   hook, which might do absolutely anything -- allocate memory, touch
 *   BOOPSI gadgets, ...) is deferred to FS3ETimer_Process(), run by the
 *   owning task in ordinary context after being woken by that Signal().
 *
 * - The running-timers list (s_timers) is written (AddHead/Remove) only
 *   from task context (FS3ETimer_Start/Stop) and only ever READ from
 *   interrupt context (FS3ETimer_IntServer just walks it to set pending
 *   flags, never mutates its linkage) -- so the list itself only needs
 *   protecting against being mutated by the task WHILE the interrupt is
 *   mid-walk, which is exactly what Disable()/Enable() around the
 *   AddHead/Remove calls achieves (same "bracket a list a VBlank/other
 *   interrupt also touches" idiom already used for MsgPort lists
 *   elsewhere in this codebase, see FS3ENet_CountPending's own comment).
 *   Disable() sections here are kept to a couple of pointer-fixup
 *   instructions, same discipline.
 *
 * - FS3ETimer_Process() itself must tolerate a hook it just called
 *   stopping (or starting) an arbitrary FS3ETimer, including itself or a
 *   not-yet-visited one further down the list -- trusting a "next"
 *   pointer captured before that call could then dangle. Instead of
 *   reasoning about which mutations are safe to survive mid-walk, it
 *   simply restarts its scan from the (always valid) list head after
 *   every single hook call; since a hook's own pending flag is cleared
 *   before it runs, the rescan can never re-invoke it, so this converges
 *   in the same number of hook calls a single clean pass would have
 *   taken, just with cheap extra O(n) rescans in between (n = number of
 *   concurrently active timers, expected to stay in the single digits).
 */

#include "fs3etimer.h"
#include "compilers.h"

#include <proto/exec.h>
#include <proto/alib.h> /* NewList() -- an amiga.lib helper, not exec.library itself */

#include <exec/interrupts.h>
#include <exec/lists.h>
#include <exec/tasks.h>
#include <hardware/intbits.h>
#include <graphics/gfxbase.h>
#include <dos/dos.h>

#include <stdio.h>
extern struct GfxBase *GfxBase;

static struct Interrupt s_vblankInt;
static struct MinList   s_timers;
static volatile ULONG   s_vblankCounter = 0;
static struct Task     *s_task = NULL;
static BOOL             s_installed = FALSE;

/* Runs at interrupt time -- see this file's header comment. A1 = is_Data
 * (unused, NULL -- every timer's own state is reached via the static
 * s_timers list instead, no per-server data needed), matching the
 * REG()/ASM/SAVEDS calling-convention macros (compilers.h) every other
 * OS-invoked callback in this codebase already uses. Returns nothing
 * meaningful -- VERTB is a server chain, not a single owned handler, so
 * there is no "did you handle it" value to hand back. */
static void ASM SAVEDS FS3ETimer_IntServer(REG(a1, APTR intData))
{
    FS3ETimer *t;
    BOOL       anyDue = FALSE;

    (void)intData;

    s_vblankCounter++;

    for (t = (FS3ETimer *)s_timers.mlh_Head;
         t->node.mln_Succ;
         t = (FS3ETimer *)t->node.mln_Succ)
    {
        if ((s_vblankCounter % t->divTick) == 0) {
            t->pending = TRUE;
            anyDue = TRUE;
        }
    }

    if (anyDue && s_task)
        Signal(s_task, FS3ETIMER_SIGNAL);
}

BOOL FS3ETimer_Init(void)
{
    if (s_installed) return TRUE;

    NewList((struct List *)&s_timers);
    s_vblankCounter = 0;
    s_task = FindTask(NULL);
    if (!s_task) return FALSE;

    s_vblankInt.is_Node.ln_Type = NT_INTERRUPT;
    s_vblankInt.is_Node.ln_Pri  = 0;
    s_vblankInt.is_Node.ln_Name = "FriendSh3ep-timer";
    s_vblankInt.is_Data         = NULL;
    s_vblankInt.is_Code         = (VOID (*)())FS3ETimer_IntServer;

    AddIntServer(INTB_VERTB, &s_vblankInt);
    s_installed = TRUE;
    return TRUE;
}

void FS3ETimer_Exit(void)
{
    if (!s_installed) return;

    RemIntServer(INTB_VERTB, &s_vblankInt);
    s_installed = FALSE;

    /* Nothing left can fire once the server is removed -- just forget
     * about whatever timers were still linked (their storage belongs to
     * callers, who are expected to be done with this module by now; see
     * this function's own doc comment in fs3etimer.h). */
    NewList((struct List *)&s_timers);
    s_task = NULL;
}

void FS3ETimer_Start(FS3ETimer *timer, FS3ETimerHook hook, ULONG divTick, ULONG totalTicks)
{
    if (!timer) return;

    if (timer->active)
        FS3ETimer_Stop(timer);



    timer->hook        = hook;
    timer->divTick      = divTick ? divTick : 1;
    timer->totalTicks   = totalTicks;
    timer->startVblank  = s_vblankCounter;
    timer->pending      = FALSE;
    timer->active       = TRUE;

    Disable();
    AddHead((struct List *)&s_timers, (struct Node *)&timer->node);
    Enable();
}

void FS3ETimer_Stop(FS3ETimer *timer)
{
    if (!timer || !timer->active) return;

    Disable();
    Remove((struct Node *)&timer->node);
    Enable();

    timer->active  = FALSE;
    timer->pending = FALSE;
}

void FS3ETimer_Process(void)
{
    FS3ETimer *t;
    BOOL       again;

    do {
        again = FALSE;

        for (t = (FS3ETimer *)s_timers.mlh_Head;
             t->node.mln_Succ;
             t = (FS3ETimer *)t->node.mln_Succ)
        {
            ULONG ticks;
            BOOL  autoStop;
            FS3ETimerHook hook;

            if (!t->pending) continue;

            t->pending = FALSE;
            ticks      = s_vblankCounter - t->startVblank;
            autoStop   = (t->totalTicks != 0 && ticks >= t->totalTicks);
            hook       = t->hook;

            if (hook) hook(t, ticks);
            if (autoStop) FS3ETimer_Stop(t);

            /* The hook (or the autoStop we just did) may have mutated
             * s_timers -- see this file's header comment for why we
             * restart from a known-good head rather than trust any
             * pointer derived from t after this point. */
            again = TRUE;
            break;
        }
    } while (again);
}

ULONG FS3ETimer_Frequency(void)
{
    if (!GfxBase) return 50; /* matches this function's own fallback doc comment */
    return (GfxBase->DisplayFlags & PAL) ? 50 : 60;
}
