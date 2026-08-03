/*
 * rgbimage.c - Fast-RAM 24bit RGB pixel-array image for FriendSh3ep.
 *
 * $VER: rgbimage.c 1.0 (07.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See rgbimage.h for the lifecycle and rendering-path rationale.
 */

#include "rgbimage.h"
#include "rgbscale.h"
#include "scalepixelarraybilinear.h"
#include "friendsh3ep.h"
#include "fs3esettings.h"
#include "bmimage.h"

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/graphics.h>
#include <proto/cybergraphics.h>
#include <proto/utf8rastport.h>

#include <exec/memory.h>
#include <dos/dos.h>
#include <graphics/gfx.h>
#include <cybergraphics/cybergraphics.h>

#define RGBIMAGE_BMP_HEADER_SIZE 54  /* BITMAPFILEHEADER(14) + BITMAPINFOHEADER(40) */

static ULONG rgbimage_get_u32le(const UBYTE *p)
{
    return (ULONG)p[0] | ((ULONG)p[1] << 8) | ((ULONG)p[2] << 16) | ((ULONG)p[3] << 24);
}

static UWORD rgbimage_get_u16le(const UBYTE *p)
{
    return (UWORD)(p[0] | (p[1] << 8));
}

void RgbImage_Free(RgbImage *img)
{
    if (!img) return;
    if (img->pixels) FreeVec(img->pixels);
    img->pixels = NULL;
    img->width  = 0;
    img->height = 0;
}

BOOL RgbImage_IsLoaded(const RgbImage *img)
{
    return (BOOL)(img && img->pixels != NULL);
}

BOOL RgbImage_LoadBmp(RgbImage *img, const char *path)
{
    UBYTE header[RGBIMAGE_BMP_HEADER_SIZE];
    BPTR  fh;
    ULONG w, h, paddedRowBytes, y;
    UBYTE *rowBuf = NULL;
    UBYTE *outBuf = NULL;

    if (!img) return FALSE;
    RgbImage_Free(img);

    if (!path || !path[0]) return FALSE;

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) return FALSE;

    if (Read(fh, header, sizeof(header)) != (LONG)sizeof(header)) {
        Close(fh);
        return FALSE;
    }

    if (header[0] != 'B' || header[1] != 'M') {
        Close(fh);
        return FALSE;
    }

    w = rgbimage_get_u32le(header + 18);
    h = rgbimage_get_u32le(header + 22);

    if (rgbimage_get_u16le(header + 28) != 24 ||  /* biBitCount */
        rgbimage_get_u32le(header + 30) != 0)     /* biCompression == BI_RGB */
    {
        Close(fh);
        return FALSE;
    }

    if (w < 1 || h < 1 || w > 32767 || h > 32767) {
        Close(fh);
        return FALSE;
    }

    paddedRowBytes = (w * 3 + 3) & ~3UL;

    rowBuf = (UBYTE *)AllocVec(paddedRowBytes, MEMF_ANY);
    if (!rowBuf) { Close(fh); return FALSE; }

    outBuf = (UBYTE *)AllocVec(w * 3 * h, MEMF_ANY);
    if (!outBuf) { FreeVec(rowBuf); Close(fh); return FALSE; }

    /* BMP rows are stored bottom-up; file row y holds image row (h-1-y).
     * Pixel order on disk is BGR (see bmimage.c's bmimage_write_bmp,
     * which produced this exact format) -- swap back to RGB here. */
    for (y = 0; y < h; y++) {
        UBYTE *dstRow;
        ULONG  x;

        if (Read(fh, rowBuf, (LONG)paddedRowBytes) != (LONG)paddedRowBytes) {
            FreeVec(rowBuf);
            FreeVec(outBuf);
            Close(fh);
            return FALSE;
        }

        dstRow = outBuf + (h - 1 - y) * w * 3;
        for (x = 0; x < w; x++) {
            dstRow[x * 3 + 0] = rowBuf[x * 3 + 2]; /* R */
            dstRow[x * 3 + 1] = rowBuf[x * 3 + 1]; /* G */
            dstRow[x * 3 + 2] = rowBuf[x * 3 + 0]; /* B */
        }
    }

    FreeVec(rowBuf);
    Close(fh);

    img->pixels = outBuf;
    img->width  = (UWORD)w;
    img->height = (UWORD)h;
    return TRUE;
}

BOOL RgbImage_LoadViaDatatype(RgbImage *img, const char *path)
{
    UBYTE *buf = NULL;
    ULONG  w = 0, h = 0;

    if (!img) return FALSE;
    RgbImage_Free(img);

    if (!path || !path[0]) return FALSE;

    if (!BmImage_ReadSourceRgb(path, &buf, &w, &h, NULL)) return FALSE;
    if (w > 32767 || h > 32767) { FreeVec(buf); return FALSE; }

    img->pixels = buf;
    img->width  = (UWORD)w;
    img->height = (UWORD)h;
    return TRUE;
}

void RgbImage_DrawScaled(const RgbImage *img, struct RastPort *rp,
                          struct Screen *screen, struct URPDrawContext *dc,
                          WORD destX, WORD destY, UWORD destW, UWORD destH)
{
    ULONG depth;

    if (!img || !img->pixels || !rp || !rp->BitMap) return;
    if (destW < 1 || destH < 1) return;

    depth = GetBitMapAttr(rp->BitMap, BMA_DEPTH);

    if (depth > 8) {
        if (app->settings.rgbDrawFunction == FS3E_RGBDRAW_INTERNAL_BILINEAR) {
            ScalePixelArrayBilinear((APTR)img->pixels, img->width, img->height,
                                     (UWORD)(img->width * 3), rp,
                                     destX, destY, destW, destH);
        } else {
            ScalePixelArray((APTR)img->pixels, img->width, img->height,
                             (UWORD)(img->width * 3), rp,
                             (UWORD)destX, (UWORD)destY, destW, destH, RECTFMT_RGB);
        }
        return;
    }

    /* Indexed screen: scale in RGB space first (higher quality than
     * scaling already-quantized pens -- see RgbScale_ToSize), remap
     * through the draw context's clutRemap[] table, then blit via
     * graphics.library/WriteChunkyPixels() (V40+/OS3.1+ -- this project
     * targets OS3.2/3.9, so the WritePixelArray8() temp-RastPort dance
     * WriteChunkyPixels() was specifically introduced to avoid is not
     * needed: no scratch BitMap, no destructive row padding, just a
     * tightly-packed pen buffer). No cybergraphics.library dependency
     * either, so this path also works on plain AGA/ECS screens with no
     * RTG board. */
    if (!screen || !dc) return;

    {
        UBYTE       *scaledRGB = NULL;
        const UBYTE *rgbForRemap;
        UBYTE       *packedPen;

        if ((UWORD)destW == img->width && (UWORD)destH == img->height) {
            rgbForRemap = img->pixels;
        } else {
            scaledRGB = (UBYTE *)AllocVec((ULONG)destW * destH * 3, MEMF_ANY);
            if (!scaledRGB) return;
            /* Indexed-screen remap path: not user-configurable, always the
             * pyramid-halve + nearest-neighbor default (see rgbscale.h). */
            RgbScale_ToSize(img->pixels, img->width, img->height,
                             scaledRGB, destW, destH, FS3E_SCALEQ_BILINEAR);
            rgbForRemap = scaledRGB;
        }

        packedPen = (UBYTE *)AllocVec((ULONG)destW * destH, MEMF_ANY);
        if (!packedPen) {
            if (scaledRGB) FreeVec(scaledRGB);
            return;
        }

        /* Cheap no-op if dc is already current for this screen/depth. */
        URPDC_SetDrawScreen(dc, screen);

        if (URPDC_RemapRGB24ToPen8(dc, rgbForRemap, packedPen,
                                    (ULONG)destW * destH) != 0) {
            WriteChunkyPixels(rp, (ULONG)destX, (ULONG)destY,
                               (ULONG)(destX + destW - 1),
                               (ULONG)(destY + destH - 1),
                               packedPen, (LONG)destW);
        }

        FreeVec(packedPen);
        if (scaledRGB) FreeVec(scaledRGB);
    }
}
