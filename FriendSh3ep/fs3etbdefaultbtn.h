#ifndef FS3ETBDEFAULTBTN_H
#define FS3ETBDEFAULTBTN_H

/*
 * fs3etbdefaultbtn -- the 4 built-in title bar button glyphs (close,
 * iconify, altpos, depth) used whenever no theme image is loaded for that
 * slot. Unlike st->tbImages[] (bitmap.image, loaded from a theme's
 * tbbuttons.png), these are penmap.image objects built from a fixed 6x6
 * 2-color pixel table baked into fs3etbdefaultbtn.c -- same BOOPSI image
 * class and construction pattern as EmojiGear/closebutton.c.
 *
 * Why these exist: button.gadget does not tolerate a live gadget going
 * from a real GA_Image back to GA_Image=NULL once one has been attached --
 * confirmed glitches/crashes switching from an image theme back to
 * "no theme". So "no theme" now means these default glyphs instead of a
 * literal NULL image, and GA_Image on the 4 title bar buttons never goes
 * to NULL again after FS3EStyle_CreateDefaultButtonImages() has run once.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>

#include "fs3estyle.h"

/* Create any not-yet-created slots in out[FS3ESTYLE_TBBUTTON_COUNT]
 * (in GID_TITLEBAR_CLOSE, ICONIFY, ALTPOS, DEPTH order), remapped to scr's
 * palette. Idempotent -- a slot that already holds an Image* is left
 * untouched, so this is safe to call on every window (re)open, not just
 * the first. These are meant to live for the whole app run: pair with
 * FS3ETBDefaultBtn_Dispose() only at final teardown, never on a theme
 * switch. No-op if PenMapBase isn't open or scr is NULL. */
void FS3ETBDefaultBtn_Create(struct Image *out[FS3ESTYLE_TBBUTTON_COUNT], struct Screen *scr);

/* Dispose whatever FS3ETBDefaultBtn_Create() built and NULL every slot.
 * Call once, at final app teardown (alongside FS3EStyle_FreeThemeImages). */
void FS3ETBDefaultBtn_Dispose(struct Image *out[FS3ESTYLE_TBBUTTON_COUNT]);

#endif /* FS3ETBDEFAULTBTN_H */
