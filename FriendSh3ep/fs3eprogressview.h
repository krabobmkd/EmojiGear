#ifndef FS3EPROGRESSVIEW_H
#define FS3EPROGRESSVIEW_H

/*
 * fs3eprogressview.c/.h - bare-bones progress bar window.
 *
 * Deliberately NOT a BOOPSI window.class/layout.gadget window like every
 * other view in this app (fs3esettingsview.c, fs3ethemeview.c, ...): just a
 * raw Intuition struct Window* from OpenWindowTags(), borderless, titleless,
 * gadgetless -- its entire surface (a quarter of the screen's width, 16
 * pixels tall, centered on the screen) is the progress bar itself, nothing
 * else. No IDCMP is requested and no _HandleInput()/_GetSignalMask() pair is
 * provided -- there is nothing to ever read from this window, so it never
 * takes part in the main event loop's Wait(). Meant for short blocking
 * operations at startup (e.g. moving/flushing the disk cache) where a full
 * BOOPSI window would be overkill.
 *
 * Usage:
 *   FS3EProgressView pv;
 *   memset(&pv, 0, sizeof(pv));
 *   if (FS3EProgressView_Open(&pv)) {
 *       ... do work, periodically ...
 *       FS3EProgressView_SetValue(&pv, (UBYTE)(bytesDone * 255UL / bytesTotal));
 *       ...
 *       FS3EProgressView_Close(&pv);
 *   }
 */

#include <exec/types.h>
#include <intuition/intuition.h>

typedef struct FS3EProgressView {
    struct Window *window;       /* NULL when not open */
    struct Screen *lockedScreen; /* LockPubScreen(NULL) result, held while open */
    LONG            width;
    LONG            height;
    UBYTE           value;       /* last value set, 0..255 == 0..100% */
} FS3EProgressView;

/* Locks the current public screen, opens the borderless progress window
 * centered on it, and draws it at 0%. Returns FALSE (window left closed) if
 * the screen can't be locked or OpenWindowTags() fails. Safe to call again
 * on an already-open view (no-op, returns TRUE). */
BOOL FS3EProgressView_Open(FS3EProgressView *pv);

/* Closes the window and releases the locked screen. Safe on an
 * already-closed/never-opened view. */
void FS3EProgressView_Close(FS3EProgressView *pv);

/* Redraws the bar for value (0..255 == 0..100%) as two RectFill()s -- pen 3
 * for the filled portion, pen 0 for the rest. No-op if the view isn't open. */
void FS3EProgressView_SetValue(FS3EProgressView *pv, UBYTE value);

#endif /* FS3EPROGRESSVIEW_H */
