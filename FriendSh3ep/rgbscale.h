#ifndef RGBSCALE_H
#define RGBSCALE_H

/*
 * rgbscale.h - shared RGB24 pixel-buffer scaling helpers for FriendSh3ep.
 *
 * Extracted out of bmimage.c so the render-path pixel-array pipeline
 * (rgbimage.c) can reuse the exact same scaling code instead of
 * duplicating it -- see FriendSh3ep/PlanToReworkThumbnails.txt.
 */

#include <exec/types.h>

/*
 * Nearest-neighbor resample of an RGB24 buffer (3 bytes/pixel, tightly
 * packed rows) using 16.16 fixed-point accumulators -- one divide per
 * axis to derive the step, then just an add + shift per pixel. Good
 * enough for thumbnail-sized images on a 68020.
 */
void RgbScale_Nearest(const UBYTE *src, ULONG srcW, ULONG srcH,
                       UBYTE *dst, ULONG dstW, ULONG dstH);

/*
 * 2x2 box-filter downscale of an RGB24 image: each output pixel is the
 * average of the 2x2 source block ((A+B+C+D)>>2 per channel, no
 * multiplication). Output = (srcW/2) x (srcH/2).
 *
 * Safe to halve in-place (dst pointing back into src's buffer): output
 * row N reads source rows 2N/2N+1, and the output footprint is 1/4 of
 * the input's, so the write head never catches the read head.
 */
void RgbScale_Halve(const UBYTE *src, ULONG srcW, ULONG srcH, ULONG srcRowBytes,
                     UBYTE *dst, ULONG dstRowBytes);

/*
 * True bilinear (2x2) resample of an RGB24 buffer from srcW x srcH to
 * dstW x dstH, written directly into dst -- no clipping, no CyberGraphX
 * pixel-format dispatch, always RGB24 in and out, so only one routine is
 * needed (unlike scalepixelarraybilinear.c's 13 rastport-format
 * variants). Same integer fixed-point technique as that file's rastport
 * blitter: one 8.8 fixed-point per-axis DDA step (via fastdiv68k.h's
 * DivuW()), every interpolation multiply a 16x16->32 unsigned mulu.w,
 * a single final >>16 per component -- see scalepixelarraybilinear.c
 * for the full rationale, this is its buffer-to-buffer twin.
 */
void RgbScale_Bilinear(const UBYTE *src, ULONG srcW, ULONG srcH,
                        UBYTE *dst, ULONG dstW, ULONG dstH);

/*
 * Scales an RGB24 buffer from srcW x srcH to exactly dstW x dstH (no
 * aspect-fit math -- callers that need box-fit compute dstW/dstH first).
 *
 * quality selects the final-pass resample technique -- values match
 * fs3esettings.h's FS3E_SCALEQ_* (not #included here to keep this module
 * dependency-free; pass app->settings.scalingQuality directly):
 *   FS3E_SCALEQ_FAST      (0) - no pre-halving: a single RgbScale_Nearest
 *                                pass straight from srcW x srcH.
 *   FS3E_SCALEQ_BILINEAR  (1) - default/current behaviour: repeatedly
 *                                box-average-halves (RgbScale_Halve) until
 *                                neither axis is more than 2x the target,
 *                                then RgbScale_Nearest for the last leg.
 *   FS3E_SCALEQ_TRILINEAR (2) - same pre-halving as BILINEAR, but
 *                                RgbScale_Bilinear for the last leg
 *                                instead of RgbScale_Nearest.
 * Any other value falls back to FS3E_SCALEQ_BILINEAR.
 *
 * Pre-halving scratch is AllocVec'd once (sized for the first halving,
 * the largest one) and freed before returning.
 */
void RgbScale_ToSize(const UBYTE *src, ULONG srcW, ULONG srcH,
                      UBYTE *dst, ULONG dstW, ULONG dstH, int quality);

#endif /* RGBSCALE_H */
