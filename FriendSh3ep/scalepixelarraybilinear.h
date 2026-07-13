#ifndef SCALEPIXELARRAYBILINEAR_H
#define SCALEPIXELARRAYBILINEAR_H

/*
 * scalepixelarraybilinear.h - integer-only bilinear scale-blit to a
 * CyberGraphX RastPort, for FriendSh3ep (see PlanToReworkThumbnails.txt).
 *
 * $VER: scalepixelarraybilinear.h 1.0 (13.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * ScalePixelArrayBilinear() is a drop-in analog of cybergraphics.library's
 * own ScalePixelArray(), with two differences:
 *
 *   - srcRect is always tightly-packed 3-bytes-per-pixel RGB (R,G,B); there
 *     is no SrcFormat parameter, ARGB/4-byte sources are not supported.
 *
 *   - every destination pixel is a true bilinear interpolation of the 4
 *     nearest source pixels (2x2 box), not nearest-neighbor -- and unlike
 *     a naive float implementation, the whole thing is integer fixed-point:
 *     no float/double anywhere, every division is a right-shift by a
 *     compile-time constant except the one-time per-axis step (computed
 *     once per call, not per pixel -- see the .c file), and every multiply
 *     is a 16x16->32 unsigned multiply (fast mulu.w on 68000/68010, not the
 *     slow 32x32 library call -- see fastdiv68k.h for the same reasoning
 *     applied to the one non-shift division).
 *
 * Designed for scale factors in the ~0.5x-2.0x range (matches this
 * project's thumbnail/icon rescaling needs). Outside that range the 2x2
 * sampling window under/over-samples and quality degrades gracefully
 * (visible aliasing on extreme downscale), same as any bilinear resizer --
 * it will not crash or read out of bounds.
 *
 * Supports every CyberGraphX truecolor pixel format LockBitMapTags() can
 * report -- RGB15/BGR15/RGB15PC/BGR15PC/RGB16/BGR16/RGB16PC/BGR16PC/RGB24/
 * BGR24/ARGB32/BGRA32/RGBA32 -- everything except PIXFMT_LUT8: indexed
 * screens have no RGB framebuffer to write into, callers on those screens
 * must remap to pens themselves (see rgbimage.c's indexed-screen path).
 *
 * Handles "windowed RastPort" clipping like any other direct framebuffer
 * writer: if rp->Layer is set, drawing is clipped to the currently-visible
 * ClipRects via DoHookClipRects() (areas covered by other windows are
 * never touched, matching graphics.library's own layer semantics); if
 * rp->Layer is NULL (screen RastPort, offscreen BitMap, etc.), drawing is
 * clipped to the BitMap's own width/height instead. Same technique as
 * libutf8rastport's urp_draw_text_cgx().
 */

#include <exec/types.h>
#include <graphics/rastport.h>

/*
 * srcRect  - tightly-packed RGB24 source pixels (3 bytes/pixel), top-down.
 * srcW/srcH- source dimensions in pixels.
 * srcMod   - bytes per source row (>= srcW*3; lets callers pass a sub-
 *            rectangle of a larger buffer without copying).
 * rp       - destination RastPort; rp->BitMap must be CyberGraphX-lockable.
 * destX/destY/destW/destH - destination rectangle, RastPort-relative
 *            (same coordinate convention as ScalePixelArray()/WritePixelArray()).
 *
 * Returns TRUE on success, FALSE if srcRect/rp is NULL, srcW/srcH/destW/
 * destH is 0, rp has no BitMap, LockBitMapTags() fails, or the locked
 * BitMap's pixel format is PIXFMT_LUT8.
 */
BOOL ScalePixelArrayBilinear(CONST APTR srcRect, UWORD srcW, UWORD srcH, UWORD srcMod,
                              struct RastPort *rp,
                              WORD destX, WORD destY, UWORD destW, UWORD destH);

#endif /* SCALEPIXELARRAYBILINEAR_H */
