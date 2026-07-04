/*
 * patch9.c - shared 9-slice ("patch9") background skin for BOOPSI gadgets.
 *
 * $VER: patch9.c 1.0 (03.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See patch9.h for the public interface and lifecycle documentation.
 */

#include "patch9.h"

#include <string.h>
#include <proto/graphics.h>

BOOL Patch9_Init(Patch9 *p9, const char *path, WORD cornerSize)
{
    if (!p9) return FALSE;
    memset(p9, 0, sizeof(*p9));
    if (!BmImage_Init(&p9->img, path)) return FALSE;
    p9->cornerSize = cornerSize;
    return TRUE;
}

BOOL Patch9_Load(Patch9 *p9, struct Screen *screen)
{
    if (!p9) return FALSE;

    p9->patchWidth  = 0;
    p9->patchHeight = 0;

    if (!BmImage_Load(&p9->img, screen)) return FALSE;

    p9->patchWidth  = (WORD)(p9->img.width / PATCH9_NUM_STATES);
    p9->patchHeight = (WORD)p9->img.height;
    return TRUE;
}

void Patch9_Unload(Patch9 *p9)
{
    if (!p9) return;
    BmImage_Unload(&p9->img);
    p9->patchWidth  = 0;
    p9->patchHeight = 0;
}

void Patch9_Free(Patch9 *p9)
{
    if (!p9) return;
    BmImage_Free(&p9->img);
    p9->cornerSize  = 0;
    p9->patchWidth  = 0;
    p9->patchHeight = 0;
}

BOOL Patch9_IsLoaded(const Patch9 *p9)
{
    return (BOOL)(p9 && p9->img.bitmap && p9->patchWidth > 0 && p9->patchHeight > 0);
}

/* Blit a srcW x srcH region, repeated left-to-right/top-to-bottom, to fill
 * a dstW x dstH area. The last tile in each direction is cropped to fit --
 * used both for edge/center tiling and (when dstW==srcW/dstH==srcH, i.e. a
 * single-iteration "tile") for the four corners. */
static void patch9_blit_tiled(struct BitMap *srcBm, PLANEPTR mask,
                              WORD srcX, WORD srcY, WORD srcW, WORD srcH,
                              struct RastPort *rp,
                              WORD dstX, WORD dstY, WORD dstW, WORD dstH)
{
    WORD y;
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    for (y = 0; y < dstH; y += srcH) {
        WORD tileH = ((dstH - y) < srcH) ? (dstH - y) : srcH;
        WORD x;
        for (x = 0; x < dstW; x += srcW) {
            WORD tileW = ((dstW - x) < srcW) ? (dstW - x) : srcW;
            if (mask) {
                BltMaskBitMapRastPort(srcBm, (LONG)srcX, (LONG)srcY,
                                      rp, (LONG)(dstX + x), (LONG)(dstY + y),
                                      (LONG)tileW, (LONG)tileH,
                                      PATCH9_MASK_MINTERM, mask);
            } else {
                BltBitMapRastPort(srcBm, (LONG)srcX, (LONG)srcY,
                                  rp, (LONG)(dstX + x), (LONG)(dstY + y),
                                  (LONG)tileW, (LONG)tileH, 0xC0);
            }
        }
    }
}

void Patch9_Draw(Patch9 *p9, int state, struct RastPort *rp,
                 WORD destX, WORD destY, WORD destW, WORD destH)
{
    struct BitMap *srcBm;
    PLANEPTR       mask;
    WORD           baseX, pw, ph, cs, cw, ch;
    WORD           midSrcW, midSrcH, midDstW, midDstH;

    if (!p9 || !rp) return;
    if (destW <= 0 || destH <= 0) return;
    if (state < 0 || state >= PATCH9_NUM_STATES) return;
    if (!Patch9_IsLoaded(p9)) return;

    srcBm = p9->img.bitmap;
    mask  = p9->img.mask;
    pw    = p9->patchWidth;
    ph    = p9->patchHeight;
    baseX = (WORD)(state * pw);

    /* Corner size clamped first to the source patch, then to the requested
     * target size, so a tiny button crops the corners instead of blitting
     * negative-size (or overlapping) middle strips. */
    cs = p9->cornerSize;
    if (cs < 0) cs = 0;
    if (cs * 2 > pw) cs = pw / 2;
    if (cs * 2 > ph) cs = ph / 2;

    cw = cs; ch = cs;
    if (cw * 2 > destW) cw = destW / 2;
    if (ch * 2 > destH) ch = destH / 2;

    /* Four corners: 1:1 copy (cropped to cw x ch if smaller than cs). */
    patch9_blit_tiled(srcBm, mask, baseX,           0,            cw, ch, rp,
                      destX,                 destY,                 cw, ch);
    patch9_blit_tiled(srcBm, mask, baseX+pw-cw,     0,            cw, ch, rp,
                      destX+destW-cw,        destY,                 cw, ch);
    patch9_blit_tiled(srcBm, mask, baseX,           ph-ch,        cw, ch, rp,
                      destX,                 destY+destH-ch,        cw, ch);
    patch9_blit_tiled(srcBm, mask, baseX+pw-cw,     ph-ch,        cw, ch, rp,
                      destX+destW-cw,        destY+destH-ch,        cw, ch);

    midSrcW = pw - 2 * cw;
    midSrcH = ph - 2 * ch;
    midDstW = destW - 2 * cw;
    midDstH = destH - 2 * ch;

    /* Top/bottom edges: tiled horizontally. */
    if (midSrcW > 0 && midDstW > 0) {
        patch9_blit_tiled(srcBm, mask, baseX+cw, 0,      midSrcW, ch, rp,
                          destX+cw,        destY,                midDstW, ch);
        patch9_blit_tiled(srcBm, mask, baseX+cw, ph-ch,  midSrcW, ch, rp,
                          destX+cw,        destY+destH-ch,       midDstW, ch);
    }

    /* Left/right edges: tiled vertically. */
    if (midSrcH > 0 && midDstH > 0) {
        patch9_blit_tiled(srcBm, mask, baseX,        ch, cw, midSrcH, rp,
                          destX,               destY+ch,       cw, midDstH);
        patch9_blit_tiled(srcBm, mask, baseX+pw-cw,  ch, cw, midSrcH, rp,
                          destX+destW-cw,      destY+ch,       cw, midDstH);
    }

    /* Center: tiled both directions. */
    if (midSrcW > 0 && midSrcH > 0 && midDstW > 0 && midDstH > 0) {
        patch9_blit_tiled(srcBm, mask, baseX+cw, ch, midSrcW, midSrcH, rp,
                          destX+cw,        destY+ch,   midDstW, midDstH);
    }
}
