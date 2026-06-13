/*
 * closebutton.c - 8x8 close-button image for the ClickTab tab bar
 * (CLICKTAB_CloseImage), built with the penmap.image BOOPSI class so it is
 * remapped to the current screen's palette.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <images/penmap.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/penmap.h>
#include <utility/tagitem.h>

#include "closebutton.h"

/*
 * penmap.image render data: 2-byte width, 2-byte height (big-endian UWORDs),
 * followed by width*height pen indices (see closePalette below).  Pen 0 is
 * mapped to the screen/window background by PENMAP_Transparent.
 *
 * Layout: a plain 8x8 rectangle - a bluish face (pen 1) surrounded by a 1px
 * frame.  The selected state only changes the frame's pen (pen 2 -> pen 3),
 * the bluish face stays the same.
 */
static const UBYTE closeNormalData[] = {
    0,8, 0,8,
    2,2,2,2,2,2,2,2,
    2,3,3,3,4,4,4,2,
    2,3,3,4,4,4,5,2,
    2,3,4,4,4,4,5,2,
    2,4,4,4,4,4,5,2,
    2,4,4,4,4,4,5,2,
    2,4,5,5,5,5,5,2,
    2,2,2,2,2,2,2,2,
};

static const UBYTE closeSelectedData[] = {
    0,8, 0,8,
    3,3,3,3,3,3,3,3,
    3,1,1,1,1,1,1,3,
    3,1,1,1,1,1,1,3,
    3,1,1,1,1,1,1,3,
    3,1,1,1,1,1,1,3,
    3,1,1,1,1,1,1,3,
    3,1,1,1,1,1,1,3,
    3,3,3,3,3,3,3,3,
};

/* PENMAP_Palette: pen count followed by that many RGB32 triplets (pen 0 is
 * implicit and replaced by the background pen via PENMAP_Transparent). */
static const ULONG closePalette[] = {
    6,
    0, 0, 0, /*       */
    0x88888888, 0x88888888, 0xdddddddd, /* 1: bluish face          */
    0, 0, 0, /* 2: frame, normal        */
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, /* 3: frame, selected      */
    0xdddddddd, 0xdddddddd, 0xdddddddd, /* 4 grey 1         */
    0x99999999, 0x99999999, 0x99999999, /* 5 grey 2         */
};

struct Image *CloseButton_CreateImage(struct Screen *scr)
{
    if (!PenMapBase || !scr) return NULL;

    return (struct Image *)NewObject(PENMAP_GetClass(), NULL,
        PENMAP_RenderData,  (ULONG)closeNormalData,
        PENMAP_SelectData,  (ULONG)closeSelectedData,
        PENMAP_Palette,     (ULONG)closePalette,
        PENMAP_Screen,      (ULONG)scr,
      PENMAP_ColorMap,(ULONG)scr->ViewPort.ColorMap,
        PENMAP_Transparent, TRUE,
        PENMAP_MaskBlit,    TRUE,
        TAG_END);
}
