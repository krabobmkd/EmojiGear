/*
 * scalepixelarraybilinear.c - integer-only bilinear scale-blit to a
 * CyberGraphX RastPort, for FriendSh3ep.
 *
 * $VER: scalepixelarraybilinear.c 1.0 (13.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See scalepixelarraybilinear.h for the public contract. Implementation
 * notes:
 *
 *   - Fixed point: SPAB_BuildAxisMap() computes one 8.8 fixed-point DDA
 *     step per axis (one DivuW() call each, see fastdiv68k.h), then
 *     walks it with plain adds to fill an array of {p0,p1,w0,w1} per
 *     destination column/row -- no division anywhere else, per pixel or
 *     otherwise. The per-pixel interpolation itself (SPAB_Lerp2x2) only
 *     ever adds/multiplies UWORDs and finishes with a single >>16.
 *
 *   - Multiplies: every multiply in the hot path goes through
 *     SPAB_MulUW(UWORD,UWORD)->ULONG, so the compiler always sees a
 *     widening 16x16->32 multiply (mulu.w on 68000/68010) rather than a
 *     32x32 one. See SPAB_Lerp2x2's comment for why every intermediate
 *     value provably fits in 16 bits.
 *
 *   - Pixel formats: PUT_<FMT> macros are write-only siblings of
 *     libutf8rastport/urp_cgx_blend.c's GET_<FMT>/PUT_<FMT> pairs (same
 *     byte layouts), generated into 13 per-format blit functions by
 *     DEF_SCALE_BLIT_FUNC, dispatched through spab_blit_table[] indexed
 *     by the CyberGfx PIXFMT_* LockBitMapTags() reports. PIXFMT_LUT8
 *     (index 0) is not handled, same as urp_cgx_blend.c.
 *
 *   - Clipping: mirrors libutf8rastport/utf8rastport.c's
 *     urp_draw_text_cgx() exactly -- rp->Layer set means clip to the
 *     live ClipRects via DoHookClipRects(), rp->Layer NULL means clip to
 *     the BitMap's own width/height. struct SPAB_BfMsg mirrors the
 *     undocumented BackFillMsg DoHookClipRects() passes to its hook
 *     (not in the AmigaOS 3.x system headers -- same situation
 *     utf8rastport.c's struct urp_bf_msg comment notes).
 */

#include "scalepixelarraybilinear.h"
#include "fastdiv68k.h"
#include "compilers.h"

#include <exec/types.h>
#include <exec/memory.h>
#include <graphics/gfx.h>
#include <graphics/rastport.h>
#include <graphics/layers.h>
#include <utility/hooks.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/cybergraphics.h>
#include <cybergraphx/cybergraphics.h>


/* =========================================================================
 * Fixed-point axis map
 * ========================================================================= */

#define SPAB_FRAC_BITS 8
#define SPAB_FRAC_ONE  (1U << SPAB_FRAC_BITS)   /* 256 */

typedef struct SPABAxisMap {
    UWORD p0, p1;   /* source pixel index pair (p1 = p0+1, clamped to srcN-1) */
    UWORD w0, w1;   /* weights toward p0/p1; always w0+w1 == SPAB_FRAC_ONE */
} SPABAxisMap;

/*
 * Fills destN entries mapping [0,destN) destination samples onto source
 * pixels [0,srcN). One division for the whole axis (not per sample):
 * DivuW() is a 32-bit-dividend/16-bit-divisor divu.w, safe as long as the
 * quotient itself fits 16 bits. For this project's documented ~0.5x-2.0x
 * scale range that quotient (source pixels advanced per destination
 * pixel, in 8.8 fixed point) never exceeds ~512 regardless of srcN's
 * absolute size -- the guard below falls back to a plain divide for any
 * caller that goes further outside that range, so this is always
 * correct, just not always the fast path.
 */
static void SPAB_BuildAxisMap(SPABAxisMap *map, UWORD destN, UWORD srcN)
{
    ULONG dividend = (ULONG)srcN << SPAB_FRAC_BITS;
    ULONG step;
    ULONG accum = 0;
    UWORD i;

    if (dividend <= 0xFFFFUL * (ULONG)destN)
        step = DivuW(dividend, destN);
    else
        step = dividend / destN;

    for (i = 0; i < destN; i++) {
        UWORD p0   = (UWORD)(accum >> SPAB_FRAC_BITS);
        UWORD frac = (UWORD)(accum & (SPAB_FRAC_ONE - 1));
        UWORD p1;

        if (p0 >= (UWORD)(srcN - 1)) {
            p0   = (UWORD)(srcN - 1);
            p1   = p0;
            frac = 0;
        } else {
            p1 = (UWORD)(p0 + 1);
        }

        map[i].p0 = p0;
        map[i].p1 = p1;
        map[i].w1 = frac;
        map[i].w0 = (UWORD)(SPAB_FRAC_ONE - frac);

        accum += step;
    }
}


/* =========================================================================
 * 16x16->32 unsigned multiply + per-component bilinear lerp
 * ========================================================================= */

INLINE ULONG SPAB_MulUW(UWORD a, UWORD b)
{
    return (ULONG)a * (ULONG)b;
}

/*
 * c00/c10/c01/c11 in [0,255]; wx0+wx1==256, wy0+wy1==256.
 *   top    = c00*wx0 + c10*wx1        (weighted avg, total weight 256:
 *   bottom = c01*wx0 + c11*wx1         max value 255*256=65280 -- fits UWORD)
 *   result = (top*wy0 + bottom*wy1) >> 16
 * top/bottom fitting in UWORD is what keeps the *second* pass of
 * multiplies within the same 16x16->32 constraint as the first. The
 * final >>16 is the only division anywhere in the per-pixel path.
 */
INLINE UBYTE SPAB_Lerp2x2(UBYTE c00, UBYTE c10, UBYTE c01, UBYTE c11,
                                  UWORD wx0, UWORD wx1, UWORD wy0, UWORD wy1)
{
    UWORD top    = (UWORD)(SPAB_MulUW(c00, wx0) + SPAB_MulUW(c10, wx1));
    UWORD bottom = (UWORD)(SPAB_MulUW(c01, wx0) + SPAB_MulUW(c11, wx1));
    ULONG result = SPAB_MulUW(top, wy0) + SPAB_MulUW(bottom, wy1);
    return (UBYTE)(result >> 16);
}


/* =========================================================================
 * Per-format PUT macros (write-only siblings of urp_cgx_blend.c's
 * GET_<FMT>/PUT_<FMT> pairs -- same byte layouts, no GET needed here).
 * ========================================================================= */

#define PUT_RGB15(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(r)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(b)>>3)); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

#define PUT_BGR15(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(b)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(r)>>3)); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

#define PUT_RGB15PC(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(r)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(b)>>3)); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

#define PUT_BGR15PC(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(b)>>3)<<10)|(((UWORD)(g)>>3)<<5)|((UWORD)(r)>>3)); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

#define PUT_RGB16(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(r)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(b)>>3)); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

#define PUT_BGR16(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(b)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(r)>>3)); \
    (p)[0]=(UBYTE)(_px>>8); (p)[1]=(UBYTE)(_px&0xFF); } while(0)

#define PUT_RGB16PC(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(r)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(b)>>3)); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

#define PUT_BGR16PC(p,r,g,b) do { \
    UWORD _px = (UWORD)((((UWORD)(b)>>3)<<11)|(((UWORD)(g)>>2)<<5)|((UWORD)(r)>>3)); \
    (p)[0]=(UBYTE)(_px&0xFF); (p)[1]=(UBYTE)(_px>>8); } while(0)

#define PUT_RGB24(p,r,g,b)  do { (p)[0]=(r); (p)[1]=(g); (p)[2]=(b); } while(0)
#define PUT_BGR24(p,r,g,b)  do { (p)[0]=(b); (p)[1]=(g); (p)[2]=(r); } while(0)

/* Alpha byte forced opaque (0xFF): srcRect has no alpha channel to carry. */
#define PUT_ARGB32(p,r,g,b) do { (p)[0]=0xFF; (p)[1]=(r); (p)[2]=(g); (p)[3]=(b); } while(0)
#define PUT_BGRA32(p,r,g,b) do { (p)[0]=(b); (p)[1]=(g); (p)[2]=(r); (p)[3]=0xFF; } while(0)
#define PUT_RGBA32(p,r,g,b) do { (p)[0]=(r); (p)[1]=(g); (p)[2]=(b); (p)[3]=0xFF; } while(0)


/* =========================================================================
 * Blit args bundle + per-format blit function generator
 * ========================================================================= */

struct SPABBlitArgs {
    UBYTE              *base;
    ULONG               bpr;
    WORD                bx1, by1, bx2, by2; /* clipped rect, base-relative absolute coords */
    WORD                destX, destY;       /* dest rect origin, same coord space as bx1..by2 */
    const SPABAxisMap  *xMap;               /* destW entries, xMap[0] == column destX */
    const SPABAxisMap  *yMap;               /* destH entries, yMap[0] == row destY */
    const UBYTE        *srcRect;
    ULONG               srcMod;
};

#define DEF_SCALE_BLIT_FUNC(NAME, BPP, PUTRGB) \
static void SPAB_Blit_##NAME(const struct SPABBlitArgs *a) \
{ \
    WORD y; \
    for (y = a->by1; y < a->by2; y++) { \
        const SPABAxisMap *ym   = &a->yMap[y - a->destY]; \
        const UBYTE       *row0 = a->srcRect + (ULONG)ym->p0 * a->srcMod; \
        const UBYTE       *row1 = a->srcRect + (ULONG)ym->p1 * a->srcMod; \
        UBYTE              *drow = a->base + (ULONG)y * a->bpr; \
        WORD x; \
        for (x = a->bx1; x < a->bx2; x++) { \
            const SPABAxisMap *xm = &a->xMap[x - a->destX]; \
            const UBYTE *p00 = row0 + (ULONG)xm->p0 * 3; \
            const UBYTE *p10 = row0 + (ULONG)xm->p1 * 3; \
            const UBYTE *p01 = row1 + (ULONG)xm->p0 * 3; \
            const UBYTE *p11 = row1 + (ULONG)xm->p1 * 3; \
            UBYTE *dp = drow + (ULONG)x * (BPP); \
            UBYTE r = SPAB_Lerp2x2(p00[0], p10[0], p01[0], p11[0], xm->w0, xm->w1, ym->w0, ym->w1); \
            UBYTE g = SPAB_Lerp2x2(p00[1], p10[1], p01[1], p11[1], xm->w0, xm->w1, ym->w0, ym->w1); \
            UBYTE b = SPAB_Lerp2x2(p00[2], p10[2], p01[2], p11[2], xm->w0, xm->w1, ym->w0, ym->w1); \
            PUTRGB(dp, r, g, b); \
        } \
    } \
}

DEF_SCALE_BLIT_FUNC(RGB15,   2, PUT_RGB15)
DEF_SCALE_BLIT_FUNC(BGR15,   2, PUT_BGR15)
DEF_SCALE_BLIT_FUNC(RGB15PC, 2, PUT_RGB15PC)
DEF_SCALE_BLIT_FUNC(BGR15PC, 2, PUT_BGR15PC)
DEF_SCALE_BLIT_FUNC(RGB16,   2, PUT_RGB16)
DEF_SCALE_BLIT_FUNC(BGR16,   2, PUT_BGR16)
DEF_SCALE_BLIT_FUNC(RGB16PC, 2, PUT_RGB16PC)
DEF_SCALE_BLIT_FUNC(BGR16PC, 2, PUT_BGR16PC)
DEF_SCALE_BLIT_FUNC(RGB24,   3, PUT_RGB24)
DEF_SCALE_BLIT_FUNC(BGR24,   3, PUT_BGR24)
DEF_SCALE_BLIT_FUNC(ARGB32,  4, PUT_ARGB32)
DEF_SCALE_BLIT_FUNC(BGRA32,  4, PUT_BGRA32)
DEF_SCALE_BLIT_FUNC(RGBA32,  4, PUT_RGBA32)

typedef void (*SPABBlitFunc)(const struct SPABBlitArgs *);

/* Indexed by PIXFMT_* (0-13); entry 0 (LUT8) is NULL: not handled --
 * indexed screens have no RGB framebuffer to write into. */
static const SPABBlitFunc spab_blit_table[14] = {
    /* 0  PIXFMT_LUT8    */ NULL,
    /* 1  PIXFMT_RGB15   */ SPAB_Blit_RGB15,
    /* 2  PIXFMT_BGR15   */ SPAB_Blit_BGR15,
    /* 3  PIXFMT_RGB15PC */ SPAB_Blit_RGB15PC,
    /* 4  PIXFMT_BGR15PC */ SPAB_Blit_BGR15PC,
    /* 5  PIXFMT_RGB16   */ SPAB_Blit_RGB16,
    /* 6  PIXFMT_BGR16   */ SPAB_Blit_BGR16,
    /* 7  PIXFMT_RGB16PC */ SPAB_Blit_RGB16PC,
    /* 8  PIXFMT_BGR16PC */ SPAB_Blit_BGR16PC,
    /* 9  PIXFMT_RGB24   */ SPAB_Blit_RGB24,
    /* 10 PIXFMT_BGR24   */ SPAB_Blit_BGR24,
    /* 11 PIXFMT_ARGB32  */ SPAB_Blit_ARGB32,
    /* 12 PIXFMT_BGRA32  */ SPAB_Blit_BGRA32,
    /* 13 PIXFMT_RGBA32  */ SPAB_Blit_RGBA32,
};


/* =========================================================================
 * Clip [proto->destX,destY,+destW,+destH) (translated by originX/originY)
 * against [cx1,cy1,cx2,cy2) and dispatch to the format's blit function.
 * originX/originY is 0 for the no-Layer path (RastPort coords == BitMap
 * coords already); for the DoHookClipRects() path it is the Layer's
 * screen-space origin, translating window-local dest coords to screen
 * coords the same way urp_draw_text_cgx()'s clip hook translates glyphs.
 * Returns FALSE only when the pixel format is unsupported (PIXFMT_LUT8);
 * an empty/off-screen intersection is not a failure, just nothing to draw.
 * ========================================================================= */
static BOOL SPAB_ClipAndBlit(const struct SPABBlitArgs *proto, ULONG pixfmt,
                              WORD cx1, WORD cy1, WORD cx2, WORD cy2,
                              WORD originX, WORD originY,
                              UWORD destW, UWORD destH)
{
    struct SPABBlitArgs a  = *proto;
    SPABBlitFunc         fn = (pixfmt < 14) ? spab_blit_table[pixfmt] : NULL;
    WORD                 dx1 = (WORD)(originX + proto->destX);
    WORD                 dy1 = (WORD)(originY + proto->destY);

    if (!fn) return FALSE;

    a.destX = dx1;
    a.destY = dy1;
    a.bx1 = (dx1 > cx1) ? dx1 : cx1;
    a.by1 = (dy1 > cy1) ? dy1 : cy1;
    a.bx2 = (WORD)((dx1 + (WORD)destW) < cx2 ? (dx1 + (WORD)destW) : cx2);
    a.by2 = (WORD)((dy1 + (WORD)destH) < cy2 ? (dy1 + (WORD)destH) : cy2);

    if (a.bx1 < a.bx2 && a.by1 < a.by2) fn(&a);

    return TRUE;
}


/* =========================================================================
 * DoHookClipRects() path
 * ========================================================================= */

/* BackFillMsg is not defined in AmigaOS 3.x system headers; mirror it here
 * (see libutf8rastport/utf8rastport.c's struct urp_bf_msg for the same
 * situation with the same DoHookClipRects() callback contract). */
struct SPAB_BfMsg {
    struct Layer     *bf_Layer;
    struct Rectangle  bf_Bounds;
    LONG              bf_OffsetX;
    LONG              bf_OffsetY;
};

struct SPABClipHook {
    struct Hook                hook; /* must be first -- a0 on entry */
    const struct SPABBlitArgs *proto;
    UWORD                       destW, destH;
    BOOL                        ok;
};

/* a0 = struct Hook *, a2 = struct RastPort *, a1 = struct SPAB_BfMsg * */
static void SPAB_ClipHookFunc(REG(a0, struct Hook          *h),
                               REG(a2, struct RastPort      *rp),
                               REG(a1, struct SPAB_BfMsg    *msg))
{
    struct SPABClipHook *hd = (struct SPABClipHook *)h;
    struct SPABBlitArgs  ctxArgs = *hd->proto;
    APTR                 handle;
    ULONG                bmwidth, bmheight, pixfmt;
    WORD                 cx1, cy1, cx2, cy2, layerX, layerY;

    handle = LockBitMapTags(rp->BitMap,
                            LBMI_PIXFMT,      (ULONG)&pixfmt,
                            LBMI_BASEADDRESS, (ULONG)&ctxArgs.base,
                            LBMI_BYTESPERROW, (ULONG)&ctxArgs.bpr,
                            LBMI_WIDTH,       (ULONG)&bmwidth,
                            LBMI_HEIGHT,      (ULONG)&bmheight,
                            TAG_DONE);
    if (!handle) { hd->ok = FALSE; return; }

    cx1 = (WORD)msg->bf_Bounds.MinX;
    cy1 = (WORD)msg->bf_Bounds.MinY;
    cx2 = (msg->bf_Bounds.MaxX + 1 < (WORD)bmwidth)  ? (WORD)(msg->bf_Bounds.MaxX + 1) : (WORD)bmwidth;
    cy2 = (msg->bf_Bounds.MaxY + 1 < (WORD)bmheight) ? (WORD)(msg->bf_Bounds.MaxY + 1) : (WORD)bmheight;

    layerX = msg->bf_Layer ? (WORD)msg->bf_Layer->bounds.MinX : 0;
    layerY = msg->bf_Layer ? (WORD)msg->bf_Layer->bounds.MinY : 0;

    if (!SPAB_ClipAndBlit(&ctxArgs, pixfmt, cx1, cy1, cx2, cy2,
                           layerX, layerY, hd->destW, hd->destH))
        hd->ok = FALSE;

    UnLockBitMap(handle);
}


/* =========================================================================
 * Public entry point
 * ========================================================================= */

BOOL ScalePixelArrayBilinear(CONST APTR srcRect, UWORD srcW, UWORD srcH, UWORD srcMod,
                              struct RastPort *rp,
                              WORD destX, WORD destY, UWORD destW, UWORD destH)
{
    SPABAxisMap         *xMap, *yMap;
    struct SPABBlitArgs  proto;
    BOOL                 result = FALSE;

    if (!srcRect || !rp || !rp->BitMap) return FALSE;
    if (srcW < 1 || srcH < 1 || destW < 1 || destH < 1) return FALSE;

    xMap = (SPABAxisMap *)AllocVec((ULONG)destW * sizeof(SPABAxisMap), MEMF_ANY);
    if (!xMap) return FALSE;
    yMap = (SPABAxisMap *)AllocVec((ULONG)destH * sizeof(SPABAxisMap), MEMF_ANY);
    if (!yMap) { FreeVec(xMap); return FALSE; }

    SPAB_BuildAxisMap(xMap, destW, srcW);
    SPAB_BuildAxisMap(yMap, destH, srcH);

    proto.base    = NULL;
    proto.bpr     = 0;
    proto.bx1     = proto.by1 = proto.bx2 = proto.by2 = 0;
    proto.destX   = destX;
    proto.destY   = destY;
    proto.xMap    = xMap;
    proto.yMap    = yMap;
    proto.srcRect = (const UBYTE *)srcRect;
    proto.srcMod  = srcMod;

    if (rp->Layer) {
        struct SPABClipHook hd;
        hd.hook.h_MinNode.mln_Succ = NULL;
        hd.hook.h_MinNode.mln_Pred = NULL;
        hd.hook.h_Entry    = (ULONG(*)())SPAB_ClipHookFunc;
        hd.hook.h_SubEntry = NULL;
        hd.hook.h_Data     = NULL;
        hd.proto = &proto;
        hd.destW = destW;
        hd.destH = destH;
        hd.ok    = TRUE; /* optimistic: a fully-obscured window calls the
                           * hook 0 times, which isn't a failure -- only an
                           * unsupported pixel format or a lock failure
                           * flips this to FALSE (see SPAB_ClipHookFunc). */
        DoHookClipRects(&hd.hook, rp, NULL);
        result = hd.ok;
    } else {
        APTR  handle;
        ULONG bmwidth, bmheight, pixfmt;

        handle = LockBitMapTags(rp->BitMap,
                                LBMI_PIXFMT,      (ULONG)&pixfmt,
                                LBMI_BASEADDRESS, (ULONG)&proto.base,
                                LBMI_BYTESPERROW, (ULONG)&proto.bpr,
                                LBMI_WIDTH,       (ULONG)&bmwidth,
                                LBMI_HEIGHT,      (ULONG)&bmheight,
                                TAG_DONE);
        if (handle) {
            result = SPAB_ClipAndBlit(&proto, pixfmt, 0, 0,
                                       (WORD)bmwidth, (WORD)bmheight,
                                       0, 0, destW, destH);
            UnLockBitMap(handle);
        }
    }

    FreeVec(xMap);
    FreeVec(yMap);
    return result;
}
