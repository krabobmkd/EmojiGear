/*
 * rgbscale.c - shared RGB24 pixel-buffer scaling helpers for FriendSh3ep.
 *
 * $VER: rgbscale.c 1.0 (07.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See rgbscale.h. Logic moved here unchanged from bmimage.c's
 * bmimage_scale_rgb_nearest()/bmimage_halve_rgb() (same technique as
 * blit_bgra_to_rgba_raw()/halve_bgra() in libutf8rastport/utf8rastport.c)
 * so rgbimage.c can reuse it without duplicating it.
 */

#include "rgbscale.h"
#include "fastdiv68k.h"

#include <proto/exec.h>
#include <exec/memory.h>

/* Values match fs3esettings.h's FS3E_SCALEQ_* -- see rgbscale.h's
 * RgbScale_ToSize() doc comment for why they're not #included here. */
#define RGBSCALE_Q_FAST      0
#define RGBSCALE_Q_BILINEAR  1
#define RGBSCALE_Q_TRILINEAR 2

void RgbScale_Nearest(const UBYTE *src, ULONG srcW, ULONG srcH,
                       UBYTE *dst, ULONG dstW, ULONG dstH)
{
    ULONG srcRowBytes = srcW * 3;
    ULONG dstRowBytes = dstW * 3;
    ULONG dx = (dstW > 0) ? (srcW << 16) / dstW : 0;
    ULONG dy = (dstH > 0) ? (srcH << 16) / dstH : 0;
    ULONG accumY = 0;
    ULONG y;

    for (y = 0; y < dstH; y++) {
        const UBYTE *srow = src + (accumY >> 16) * srcRowBytes;
        UBYTE *dp = dst + y * dstRowBytes;
        ULONG accumX = 0;
        ULONG x;

        for (x = 0; x < dstW; x++) {
            const UBYTE *sp = srow + (accumX >> 16) * 3;
            dp[0] = sp[0];
            dp[1] = sp[1];
            dp[2] = sp[2];
            dp += 3;
            accumX += dx;
        }
        accumY += dy;
    }
}

void RgbScale_Halve(const UBYTE *src, ULONG srcW, ULONG srcH, ULONG srcRowBytes,
                     UBYTE *dst, ULONG dstRowBytes)
{
    const UBYTE *srcY = src;
    UBYTE       *dstY = dst;
    ULONG y, x;

    for (y = 0; y < srcH / 2; y++) {
        const UBYTE *row0 = srcY;
        const UBYTE *row1 = srcY + srcRowBytes;
        UBYTE       *dp   = dstY;

        for (x = 0; x < srcW / 2; x++) {
            dp[0] = (UBYTE)((row0[0] + row0[3 + 0] + row1[0] + row1[3 + 0]) >> 2);
            dp[1] = (UBYTE)((row0[1] + row0[3 + 1] + row1[1] + row1[3 + 1]) >> 2);
            dp[2] = (UBYTE)((row0[2] + row0[3 + 2] + row1[2] + row1[3 + 2]) >> 2);
            dp += 3;
            row0 += 6; /* advance 2 source pixels */
            row1 += 6;
        }
        srcY += srcRowBytes * 2;
        dstY += dstRowBytes;
    }
}

/* =========================================================================
 * RgbScale_Bilinear -- true bilinear (2x2) RGB24 buffer-to-buffer resample.
 * Buffer-to-buffer twin of scalepixelarraybilinear.c's rastport blitter:
 * same 8.8 fixed-point axis map + 16x16->32 unsigned-multiply lerp, just
 * without the CyberGraphX pixel-format dispatch or DoHookClipRects()
 * layer clipping (dst is always a tightly-packed RGB24 buffer, so only
 * one routine variant is needed instead of thirteen).
 * ========================================================================= */

#define RGBSCALE_FRAC_BITS 8
#define RGBSCALE_FRAC_ONE  (1U << RGBSCALE_FRAC_BITS)   /* 256 */

typedef struct RgbScaleAxis {
    UWORD p0, p1;   /* source pixel index pair (p1 = p0+1, clamped to srcN-1) */
    UWORD w0, w1;   /* weights toward p0/p1; always w0+w1 == RGBSCALE_FRAC_ONE */
} RgbScaleAxis;

/* One DivuW() per axis (32-bit dividend, guaranteed-16-bit quotient for
 * this project's thumbnail-sized scale ratios -- see fastdiv68k.h and
 * scalepixelarraybilinear.c's SPAB_BuildAxisMap for the same reasoning),
 * then a plain-add DDA walk fills the per-destination-sample map -- no
 * division anywhere else, per pixel or otherwise. */
static void rgbscale_build_axis(RgbScaleAxis *map, UWORD destN, UWORD srcN)
{
    ULONG dividend = (ULONG)srcN << RGBSCALE_FRAC_BITS;
    ULONG step;
    ULONG accum = 0;
    UWORD i;

    if (dividend <= 0xFFFFUL * (ULONG)destN)
        step = DivuW(dividend, destN);
    else
        step = dividend / destN;

    for (i = 0; i < destN; i++) {
        UWORD p0   = (UWORD)(accum >> RGBSCALE_FRAC_BITS);
        UWORD frac = (UWORD)(accum & (RGBSCALE_FRAC_ONE - 1));
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
        map[i].w0 = (UWORD)(RGBSCALE_FRAC_ONE - frac);

        accum += step;
    }
}

/* c00/c10/c01/c11 in [0,255]; wx0+wx1==256, wy0+wy1==256 -- see
 * scalepixelarraybilinear.c's SPAB_Lerp2x2 for why top/bottom are
 * guaranteed to fit UWORD, keeping every multiply here 16x16->32. */
static UBYTE rgbscale_lerp2x2(UBYTE c00, UBYTE c10, UBYTE c01, UBYTE c11,
                               UWORD wx0, UWORD wx1, UWORD wy0, UWORD wy1)
{
    UWORD top    = (UWORD)((ULONG)c00 * (ULONG)wx0 + (ULONG)c10 * (ULONG)wx1);
    UWORD bottom = (UWORD)((ULONG)c01 * (ULONG)wx0 + (ULONG)c11 * (ULONG)wx1);
    ULONG result = (ULONG)top * (ULONG)wy0 + (ULONG)bottom * (ULONG)wy1;
    return (UBYTE)(result >> 16);
}

void RgbScale_Bilinear(const UBYTE *src, ULONG srcW, ULONG srcH,
                        UBYTE *dst, ULONG dstW, ULONG dstH)
{
    ULONG         srcRowBytes = srcW * 3;
    ULONG         dstRowBytes = dstW * 3;
    RgbScaleAxis *xMap, *yMap;
    ULONG         x, y;

    if (!src || !dst) return;
    if (srcW < 1 || srcH < 1 || dstW < 1 || dstH < 1) return;
    if (srcW > 0xFFFFUL || srcH > 0xFFFFUL || dstW > 0xFFFFUL || dstH > 0xFFFFUL) return;

    xMap = (RgbScaleAxis *)AllocVec(dstW * sizeof(RgbScaleAxis), MEMF_ANY);
    if (!xMap) return;
    yMap = (RgbScaleAxis *)AllocVec(dstH * sizeof(RgbScaleAxis), MEMF_ANY);
    if (!yMap) { FreeVec(xMap); return; }

    rgbscale_build_axis(xMap, (UWORD)dstW, (UWORD)srcW);
    rgbscale_build_axis(yMap, (UWORD)dstH, (UWORD)srcH);

    for (y = 0; y < dstH; y++) {
        const RgbScaleAxis *ym   = &yMap[y];
        const UBYTE        *row0 = src + (ULONG)ym->p0 * srcRowBytes;
        const UBYTE        *row1 = src + (ULONG)ym->p1 * srcRowBytes;
        UBYTE               *dp  = dst + y * dstRowBytes;

        for (x = 0; x < dstW; x++) {
            const RgbScaleAxis *xm  = &xMap[x];
            const UBYTE        *p00 = row0 + (ULONG)xm->p0 * 3;
            const UBYTE        *p10 = row0 + (ULONG)xm->p1 * 3;
            const UBYTE        *p01 = row1 + (ULONG)xm->p0 * 3;
            const UBYTE        *p11 = row1 + (ULONG)xm->p1 * 3;

            dp[0] = rgbscale_lerp2x2(p00[0], p10[0], p01[0], p11[0], xm->w0, xm->w1, ym->w0, ym->w1);
            dp[1] = rgbscale_lerp2x2(p00[1], p10[1], p01[1], p11[1], xm->w0, xm->w1, ym->w0, ym->w1);
            dp[2] = rgbscale_lerp2x2(p00[2], p10[2], p01[2], p11[2], xm->w0, xm->w1, ym->w0, ym->w1);
            dp += 3;
        }
    }

    FreeVec(xMap);
    FreeVec(yMap);
}

void RgbScale_ToSize(const UBYTE *src, ULONG srcW, ULONG srcH,
                      UBYTE *dst, ULONG dstW, ULONG dstH, int quality)
{
    const UBYTE *curBuf      = src;
    ULONG        curW        = srcW;
    ULONG        curH        = srcH;
    ULONG        curRowBytes = srcW * 3;
    UBYTE       *scratch     = NULL;

    if (quality == RGBSCALE_Q_FAST) {
        RgbScale_Nearest(src, srcW, srcH, dst, dstW, dstH);
        return;
    }

    while (curW > dstW * 2 || curH > dstH * 2) {
        ULONG halfW, halfH, halfRowBytes;

        halfW = curW / 2;
        halfH = curH / 2;
        if (halfW < 1 || halfH < 1) break;
        halfRowBytes = halfW * 3;

        if (!scratch) {
            scratch = (UBYTE *)AllocVec(halfRowBytes * halfH, MEMF_ANY);
            if (!scratch) break; /* fall back to final pass straight from curBuf */
        }

        RgbScale_Halve(curBuf, curW, curH, curRowBytes, scratch, halfRowBytes);

        curBuf      = scratch;
        curRowBytes = halfRowBytes;
        curW        = halfW;
        curH        = halfH;
    }

    if (quality == RGBSCALE_Q_TRILINEAR)
        RgbScale_Bilinear(curBuf, curW, curH, dst, dstW, dstH);
    else
        RgbScale_Nearest(curBuf, curW, curH, dst, dstW, dstH);

    if (scratch) FreeVec(scratch);
}
