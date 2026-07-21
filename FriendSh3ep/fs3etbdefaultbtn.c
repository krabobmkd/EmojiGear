/*
 * fs3etbdefaultbtn.c -- see fs3etbdefaultbtn.h for why these exist.
 *
 * Each glyph is a 6x6 pixel penmap.image render, in the same raw format as
 * EmojiGear/closebutton.c: 2-byte width, 2-byte height (big-endian UWORDs),
 * followed by width*height pen indices. Pen 0 is transparent (mapped to the
 * screen/window background via PENMAP_Transparent); pen 1 draws the glyph
 * normally, pen 2 draws it in the "selected" (pressed) render.
 */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/classusr.h>
#include <images/penmap.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/penmap.h>
#include <utility/tagitem.h>

#include "fs3etbdefaultbtn.h"

extern struct Library *PenMapBase;

/* PENMAP_Palette: pen count followed by that many RGB32 triplets (pen 0 is
 * implicit/transparent, not listed here -- see PENMAP_Transparent). */
static const ULONG tbGlyphPalette[] = {
    2,
    0x00000000, 0x00000000, 0x00000000, /* 1: glyph, normal   -- black */
    0x00FFFFFF, 0x00FFFFFF, 0x00FFFFFF, /* 2: glyph, selected -- white */
};

#define TBGLYPH_HDR 0,6, 0,6

/* close: X */
static const UBYTE closeNormalData[] = {
    TBGLYPH_HDR,
    1,0,0,0,0,1,
    0,1,0,0,1,0,
    0,0,1,1,0,0,
    0,0,1,1,0,0,
    0,1,0,0,1,0,
    1,0,0,0,0,1,
};
static const UBYTE closeSelectedData[] = {
    TBGLYPH_HDR,
    2,0,0,0,0,2,
    0,2,0,0,2,0,
    0,0,2,2,0,0,
    0,0,2,2,0,0,
    0,2,0,0,2,0,
    2,0,0,0,0,2,
};

/* iconify: underscore bar */
static const UBYTE iconifyNormalData[] = {
    TBGLYPH_HDR,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    1,1,1,1,1,1,
};
static const UBYTE iconifySelectedData[] = {
    TBGLYPH_HDR,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    0,0,0,0,0,0,
    2,2,2,2,2,2,
};

/* altpos: diagonal double-headed arrow (move to alternate position) */
static const UBYTE altposNormalData[] = {
    TBGLYPH_HDR,
    1,1,0,0,0,0,
    1,0,1,0,0,0,
    0,1,0,1,0,0,
    0,0,1,0,1,0,
    0,0,0,1,0,1,
    0,0,0,0,1,1,
};
static const UBYTE altposSelectedData[] = {
    TBGLYPH_HDR,
    2,2,0,0,0,0,
    2,0,2,0,0,0,
    0,2,0,2,0,0,
    0,0,2,0,2,0,
    0,0,0,2,0,2,
    0,0,0,0,2,2,
};

/* depth: two overlapping squares (window depth arrange) */
static const UBYTE depthNormalData[] = {
    TBGLYPH_HDR,
    1,1,1,0,0,0,
    1,0,1,0,0,0,
    1,1,1,1,1,0,
    0,0,1,0,1,0,
    0,0,1,1,1,0,
    0,0,0,0,0,0,
};
static const UBYTE depthSelectedData[] = {
    TBGLYPH_HDR,
    2,2,2,0,0,0,
    2,0,2,0,0,0,
    2,2,2,2,2,0,
    0,0,2,0,2,0,
    0,0,2,2,2,0,
    0,0,0,0,0,0,
};

typedef struct {
    const UBYTE *normal;
    const UBYTE *selected;
} FS3ETBGlyph;

/* Same order as GID_TITLEBAR_CLOSE/ICONIFY/ALTPOS/DEPTH -- see
 * FS3ESTYLE_TBBUTTON_COUNT and FS3EStyle_SyncTitleBarButtons. */
static const FS3ETBGlyph tbGlyphs[FS3ESTYLE_TBBUTTON_COUNT] = {
    { closeNormalData,   closeSelectedData },
    { iconifyNormalData, iconifySelectedData },
    { altposNormalData,  altposSelectedData },
    { depthNormalData,   depthSelectedData },
};

void FS3ETBDefaultBtn_Create(struct Image *out[FS3ESTYLE_TBBUTTON_COUNT], struct Screen *scr)
{
    int i;

    if (!PenMapBase || !scr || !out) return;

    for (i = 0; i < FS3ESTYLE_TBBUTTON_COUNT; i++) {
        if (out[i]) continue; /* already built -- persistent, keep it */

        out[i] = (struct Image *)NewObject(PENMAP_GetClass(), NULL,
            PENMAP_RenderData,  (ULONG)tbGlyphs[i].normal,
            PENMAP_SelectData,  (ULONG)tbGlyphs[i].selected,
            PENMAP_Palette,     (ULONG)tbGlyphPalette,
            PENMAP_Screen,      (ULONG)scr,
            PENMAP_ColorMap,    (ULONG)scr->ViewPort.ColorMap,
            PENMAP_Transparent, TRUE,
            PENMAP_MaskBlit,    TRUE,
            TAG_END);
    }
}

void FS3ETBDefaultBtn_Dispose(struct Image *out[FS3ESTYLE_TBBUTTON_COUNT])
{
    int i;
    if (!out) return;
    for (i = 0; i < FS3ESTYLE_TBBUTTON_COUNT; i++) {
        if (out[i]) {
            DisposeObject((Object *)out[i]);
            out[i] = NULL;
        }
    }
}
