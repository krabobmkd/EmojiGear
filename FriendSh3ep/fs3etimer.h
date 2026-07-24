#ifndef FS3ETIMER_H
#define FS3ETIMER_H

/*
 * fs3etimer.h - VBlank-driven periodic timer service for FriendSh3ep.
 *
 * One shared vertical-blank interrupt server drives any number of
 * independent FS3ETimer instances, each with its own divisor (run the
 * hook every divTick-th vblank -- e.g. divTick=2 at a 50Hz PAL vblank
 * gives a 25Hz hook, divTick=2 at 60Hz NTSC gives 30Hz) and its own
 * optional tick budget (auto-stop after totalTicks vblanks have elapsed
 * since Start()). Meant for two kinds of use:
 *   - a repeating animation timer (e.g. autoscroll): divTick ~2,
 *     totalTicks = 0 (runs until FS3ETimer_Stop() is called)
 *   - a one-shot "wait N seconds then do something" timer: totalTicks =
 *     N * FS3ETimer_Frequency(), divTick chosen as coarse as the caller
 *     likes (even 1) since only the final call (ticks >= totalTicks)
 *     matters
 *
 * The interrupt itself does the absolute minimum safe to do from
 * interrupt context: bump a shared counter, mark any due timer
 * "pending", and Signal() the owning task once. All real work --
 * calling hooks, starting/stopping timers -- happens in normal task
 * context, via FS3ETimer_Process(), which the caller must invoke
 * whenever FS3ETIMER_SIGNAL comes back from Wait(). See fs3etimer.c's
 * own header comment for the interrupt-safety reasoning in full.
 */

#include <exec/types.h>
#include <exec/lists.h>

struct FS3ETimer;

/* ticks = number of vblanks elapsed since FS3ETimer_Start() at the
 * moment of this call -- NOT a call-count. Using raw elapsed vblanks
 * (rather than "how many times has this hook fired") lets the hook (or
 * the totalTicks auto-stop check) convert directly to/from real elapsed
 * time via FS3ETimer_Frequency(), regardless of divTick. */
typedef void (*FS3ETimerHook)(struct FS3ETimer *timer, ULONG ticks);

typedef struct FS3ETimer {
    struct MinNode node;        /* linkage into the running-timers list --
                                  * internal, do not touch */
    FS3ETimerHook  hook;
    ULONG          divTick;     /* run hook every divTick-th vblank (>=1) */
    ULONG          totalTicks;  /* auto-stop once elapsed ticks >= this;
                                  * 0 = run until FS3ETimer_Stop() */
    ULONG          startVblank; /* module's vblank counter value at Start() */
    volatile BOOL  pending;     /* set by the interrupt, consumed by
                                  * FS3ETimer_Process() -- do not touch */
    BOOL           active;      /* TRUE while linked into the running list */
} FS3ETimer;

/* OR this into the main event loop's Wait() mask, and call
 * FS3ETimer_Process() whenever it comes back set (see friendsh3ep.c's
 * main loop for where every other subsystem's signal is folded in the
 * same way). Not used by any other subsystem in this codebase as of
 * this writing (SIGBREAKF_CTRL_C/F are already claimed -- see main()). */
#define FS3ETIMER_SIGNAL SIGBREAKF_CTRL_D

/* Installs the shared VBlank interrupt server. Call once at startup,
 * after the task that will own every timer (normally the main task) is
 * the current task -- FS3ETimer_Process() must be driven from that same
 * task's event loop. Cheap and harmless if no timer is ever started.
 * Returns FALSE only if AddIntServer's own prerequisites aren't met
 * (SysBase unavailable) -- not expected to fail in practice. */
BOOL FS3ETimer_Init(void);

/* Removes the interrupt server. Any FS3ETimer still active at this point
 * stops receiving ticks (their storage is caller-owned, not freed here --
 * call FS3ETimer_Stop() on each one first if that matters to the caller). */
void FS3ETimer_Exit(void);

/* Starts (or restarts, from tick 0) timer: hook will be called from
 * FS3ETimer_Process() every divTick-th vblank (divTick clamped to a
 * minimum of 1), until totalTicks vblanks have elapsed since this call
 * (0 = unbounded, runs until FS3ETimer_Stop()). timer's storage is
 * owned by the caller -- a plain struct, no allocation here -- and must
 * stay valid and not move until stopped (or the module exits). Safe to
 * call on an already-running timer. */
void FS3ETimer_Start(FS3ETimer *timer, FS3ETimerHook hook, ULONG divTick, ULONG totalTicks);

/* Stops timer if running; a harmless no-op otherwise, including if timer
 * was never started. Safe to call from inside the timer's own hook (to
 * stop itself) or from inside another timer's hook (to stop a
 * different one). */
void FS3ETimer_Stop(FS3ETimer *timer);

/* Call from the main loop whenever FS3ETIMER_SIGNAL is set in the value
 * Wait() returned -- runs every currently-due timer's hook, in normal
 * task context (never from the interrupt itself), and auto-stops any
 * timer whose totalTicks budget was just reached. */
void FS3ETimer_Process(void);

/* 50 (PAL) or 60 (NTSC), read from graphics.library's GfxBase ->
 * DisplayFlags. Falls back to 50 if GfxBase isn't open yet. Not needed
 * to use the API -- just a convenience for callers converting a desired
 * real-time duration into a totalTicks/divTick value. */
ULONG FS3ETimer_Frequency(void);

#endif /* FS3ETIMER_H */
