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
 * Scales an RGB24 buffer from srcW x srcH to exactly dstW x dstH (no
 * aspect-fit math -- callers that need box-fit compute dstW/dstH first).
 * Repeatedly box-average-halves (RgbScale_Halve) until neither axis is
 * more than 2x the target, so the final RgbScale_Nearest pass never has
 * to skip more than one source pixel out of two -- averages the source
 * instead of dropping most of it. Scratch is AllocVec'd once (sized for
 * the first halving, the largest one) and freed before returning.
 */
void RgbScale_ToSize(const UBYTE *src, ULONG srcW, ULONG srcH,
                      UBYTE *dst, ULONG dstW, ULONG dstH);

#endif /* RGBSCALE_H */
