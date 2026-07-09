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

#include <proto/exec.h>
#include <exec/memory.h>

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

void RgbScale_ToSize(const UBYTE *src, ULONG srcW, ULONG srcH,
                      UBYTE *dst, ULONG dstW, ULONG dstH)
{
    const UBYTE *curBuf      = src;
    ULONG        curW        = srcW;
    ULONG        curH        = srcH;
    ULONG        curRowBytes = srcW * 3;
    UBYTE       *scratch     = NULL;

    while (curW > dstW * 2 || curH > dstH * 2) {
        ULONG halfW, halfH, halfRowBytes;

        halfW = curW / 2;
        halfH = curH / 2;
        if (halfW < 1 || halfH < 1) break;
        halfRowBytes = halfW * 3;

        if (!scratch) {
            scratch = (UBYTE *)AllocVec(halfRowBytes * halfH, MEMF_ANY);
            if (!scratch) break; /* fall back to NN straight from curBuf */
        }

        RgbScale_Halve(curBuf, curW, curH, curRowBytes, scratch, halfRowBytes);

        curBuf      = scratch;
        curRowBytes = halfRowBytes;
        curW        = halfW;
        curH        = halfH;
    }

    RgbScale_Nearest(curBuf, curW, curH, dst, dstW, dstH);
    if (scratch) FreeVec(scratch);
}
