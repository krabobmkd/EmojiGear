#ifndef RGBIMAGE_H
#define RGBIMAGE_H

/*
 * rgbimage.h - Fast-RAM 24bit RGB pixel-array image for FriendSh3ep's
 * render-path rework (see PlanToReworkThumbnails.txt steps 2-3).
 *
 * Unlike BmImage (bmimage.h), which decodes into a screen-format
 * struct BitMap via picture.datatype, RgbImage holds a plain top-down
 * RGB24 buffer in Fast RAM -- no chip-RAM struct BitMap, no
 * picture.datatype object kept alive, and no per-DPI on-disk thumbnail
 * cache: the same fixed-size source buffer is rescaled at draw time to
 * whatever size the current avatarSize needs.
 *
 * RgbImage_LoadBmp() reads the exact minimal 24bpp BI_RGB format
 * bmimage.c's BmImage_GenerateScaledBmp()/the thumbnail process (fs3ethumb.h)
 * writes, so no datatypes.library round-trip is needed to reload it -- only
 * that hand-written format is supported, not arbitrary BMP files.
 */

#include <exec/types.h>
#include <graphics/rastport.h>
#include <intuition/intuition.h>
#include <libraries/utf8rastport.h>

typedef struct RgbImage {
    UBYTE *pixels; /* AllocVec'd; top-down, 3 bytes/pixel (R,G,B), tightly
                     * packed rows (no padding); NULL if not loaded */
    UWORD  width;
    UWORD  height;
} RgbImage;

/*
 * Loads path (must be the minimal 24bpp BI_RGB BMP format bmimage.c
 * writes) into img. Frees any previously loaded buffer first.
 * Returns FALSE (img left unloaded) on I/O error or if the file isn't
 * that exact format (magic/bit depth/compression mismatch).
 */
BOOL RgbImage_LoadBmp(RgbImage *img, const char *path);

/* Frees img->pixels (if any) and zeroes the struct. */
void RgbImage_Free(RgbImage *img);

/* TRUE once RgbImage_LoadBmp() has succeeded and not been Free()'d since. */
BOOL RgbImage_IsLoaded(const RgbImage *img);

/*
 * Box-fit-scale-draws img into rp at (destX,destY), sized destW x destH
 * (caller has already computed the box-fit rectangle -- this always fills
 * exactly that rectangle, no aspect-ratio math here).
 *
 * depth>8 (truecolor) RastPorts: scaled directly via
 * cybergraphics.library/ScalePixelArray(RECTFMT_RGB) -- screen/dc are
 * unused in this path. If app->settings.rgbDrawFunction is
 * FS3E_RGBDRAW_INTERNAL_BILINEAR, scalepixelarraybilinear.c's
 * ScalePixelArrayBilinear() is used instead (integer-only bilinear,
 * see fs3esettingsview.c's "Thumbnails & icons" group).
 *
 * depth<=8 (indexed) RastPorts: img is first box-fit-scaled in RGB space
 * (RgbScale_ToSize, same routine bmimage.c uses -- higher quality than
 * scaling already-quantized pens), then each pixel is remapped to a
 * screen pen via URPDC_RemapRGB24ToPen8(dc, ...), then blitted with
 * graphics.library/WriteChunkyPixels() (V40+/OS3.1+ -- no scratch
 * temp-RastPort or destructive row-padding requirements, unlike the
 * older WritePixelArray8()). screen/dc must be non-NULL; dc's colour map
 * is refreshed for screen via URPDC_SetDrawScreen() first (a cheap no-op
 * if already current for this screen/depth).
 *
 * No-op if img isn't loaded, rp has no BitMap, or destW/destH is 0.
 */
void RgbImage_DrawScaled(const RgbImage *img, struct RastPort *rp,
                          struct Screen *screen, struct URPDrawContext *dc,
                          WORD destX, WORD destY, UWORD destW, UWORD destH);

#endif /* RGBIMAGE_H */
