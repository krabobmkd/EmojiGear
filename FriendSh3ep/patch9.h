/*
 * patch9.h - shared 9-slice ("patch9") background skin for BOOPSI gadgets.
 *
 * $VER: patch9.h 1.0 (03.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * A Patch9 wraps one BmImage holding PATCH9_NUM_STATES equal-size sub-images
 * arranged left-to-right in a single horizontal strip (index 0 leftmost).
 * Each sub-image is a 9-sliced skin: a cornerSize-pixel border kept unscaled
 * at the 4 corners, with the remaining edge/center strips tiled (repeated,
 * not stretched) to fill whatever target size Patch9_Draw() is asked for.
 *
 * Lifecycle mirrors BmImage:
 *   Patch9_Init()   -- once, copies file path, sets corner size
 *   Patch9_Load()   -- on each screen open; loads the bitmap + mask
 *   Patch9_Unload() -- on iconify / screen close
 *   Patch9_Free()   -- once at teardown
 *
 * A Patch9 instance is owned and kept alive by the application, not by any
 * gadget that references it -- UBTP9_Patch9 stores a borrowed pointer, so
 * one Patch9 (one skin) can be shared by any number of UniButtonP9 gadgets.
 */

#ifndef PATCH9_H
#define PATCH9_H

#include <exec/types.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <hardware/blit.h>

#include "bmimage.h"
#include "fs3estyle.h"

/* graphics.doc, BltMaskBitMapRastPort: minterm must be exactly
 * (ABC|ABNC|ANBC) to copy the source through the mask -- the plain "copy
 * source" minterm used for a normal BltBitMapRastPort (0xC0) is a
 * different, unrelated convention and is NOT valid here. Shared by
 * patch9.c and any other masked-blit caller (e.g. UniButtonP9's own
 * text-over-skin composite). */
#define PATCH9_MASK_MINTERM (ABC|ABNC|ANBC)


/*
 * Initialise from a file path and corner size. Does NOT load the image --
 * call Patch9_Load() to do that. Returns FALSE if BmImage_Init() fails.
 */
BOOL Patch9_Init(Patch9 *p9, const char *path, WORD cornerSize);

/*
 * Load (or reload) the bitmap, remapped to screen's depth and format, and
 * compute patchWidth/patchHeight. Pass screen=NULL to load raw. Returns
 * FALSE if BmImage_Load() fails (patchWidth/patchHeight are left at 0).
 */
BOOL Patch9_Load(Patch9 *p9, struct Screen *screen);

/* Dispose the loaded bitmap; file path and corner size are kept. */
void Patch9_Unload(Patch9 *p9);

/* Unload and free the file path. Leaves *p9 zeroed. */
void Patch9_Free(Patch9 *p9);

/* TRUE once Patch9_Load() has succeeded and not been Unload()/Free()'d since. */
BOOL Patch9_IsLoaded(const Patch9 *p9);

/*
 * Blit the 9-sliced `state` (PATCH9_*) sub-image to fill a destW x destH
 * rectangle at (destX,destY) on rp. Corners are copied 1:1 (cropped down
 * if destW/destH is smaller than 2*cornerSize); edges and center are
 * tiled to fill. No-op if the image isn't loaded, state is out of range,
 * or destW/destH <= 0.
 */
void Patch9_Draw(Patch9 *p9, int state, struct RastPort *rp,
                 WORD destX, WORD destY, WORD destW, WORD destH);

#endif /* PATCH9_H */
