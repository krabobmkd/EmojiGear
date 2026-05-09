/*
 * urp_cgx_blend.h  –  CyberGraphX alpha-blend dispatcher for libutf8rastport.
 *
 * Exports the blend vtable and context struct used by the CGX drawing path
 * in utf8rastport.c.  LockBitMapTags / UnLockBitMap are the caller's
 * responsibility (done once per string, outside the glyph loop).
 *
 * Two blend routines are generated for each of the 13 non-LUT8 formats:
 *   gray_<FMT>  –  source is URP_CACHE_GRAY (1 byte alpha, fg colour from dc)
 *   rgba_<FMT>  –  source is URP_CACHE_RGBA (R,G,B,A per pixel)
 *
 * Must only be called when CyberGfxBase != NULL.
 */

#ifndef URP_CGX_BLEND_H
#define URP_CGX_BLEND_H

#include <exec/types.h>
#include <graphics/rastport.h>

#include "urp_internal.h"   /* URPDrawContext, URPGlyphEntry */
#include "compilers.h"

/* -------------------------------------------------------------------------
 * Blend context
 *
 * Split into two groups:
 *   Bitmap fields  – set once after LockBitMapTags succeeds, constant per string
 *   Glyph fields   – updated per glyph before dispatching
 * ------------------------------------------------------------------------- */

struct urp_blend_ctx {
    /* bitmap fields – constant for the lifetime of the lock */
    UBYTE  *base;           /* locked bitmap base address               */
    ULONG   bpr;            /* bytes per row of locked bitmap           */
    WORD    cx1, cy1;       /* clip top-left  (screen coords, inclusive) */
    WORD    cx2, cy2;       /* clip bot-right (screen coords, exclusive) */
    /* glyph fields – updated per glyph */
    const struct URPGlyphEntry *ge; /* glyph entry (pixels, pitch, width, rows) */
    WORD    dx, dy;         /* glyph top-left in screen (bitmap) coords */
};


/* -------------------------------------------------------------------------
 * Pixel-blend function pointer type
 *
 * Both parameters are read-only.  dc carries the foreground colour
 * (dc->draw.argb.R/G/B) used by GRAY blenders; RGBA blenders ignore dc.
 * ------------------------------------------------------------------------- */

typedef void (*urp_pixblend_fn)(REG(a0, const struct urp_blend_ctx *),
                                REG(a1, const struct URPDrawContext *));


/* -------------------------------------------------------------------------
 * Blend vtable entry (one per pixel format)
 * ------------------------------------------------------------------------- */

struct urp_blend_vtable {
    urp_pixblend_fn gray;
    urp_pixblend_fn rgba;
};


/* -------------------------------------------------------------------------
 * Dispatch vtable  (indexed by PIXFMT_* value, 0-13)
 *
 * Entry 0 (PIXFMT_LUT8) has NULL entries – not handled here.
 * Defined in urp_cgx_blend.c.
 * ------------------------------------------------------------------------- */

extern const struct urp_blend_vtable urp_blend_table[14];


#endif /* URP_CGX_BLEND_H */
