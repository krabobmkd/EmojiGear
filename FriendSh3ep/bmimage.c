/*
 * bmimage.c - bitmap image loader via picture.datatype for FriendSh3ep.
 *
 * $VER: bmimage.c 1.2 (06.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * See bmimage.h for the public interface and lifecycle documentation.
 *
 * datatypes.library v44 is opened and closed via libraryTable in friendsh3ep.c.
 *
 * PDTA_DestBitMap (screen-remapped) is preferred over PDTA_BitMap (raw).
 * When screen==NULL, no remapping is requested and PDTA_BitMap is returned.
 * PDTA_FreeSourceBitMap is set so picture.datatype discards the intermediate
 * decoded bitmap after remapping, saving memory.
 *
 * img->bitmap is owned by the datatype object (dtObject) and must NOT be
 * passed to FreeBitMap().  It becomes invalid after BmImage_Unload().
 *
 * img->mask (PDTA_MaskPlane) is picture.datatype's own transparency mask,
 * generated from the source's transparent colour (e.g. a PNG palette entry
 * flagged transparent) or alpha channel.  Also owned by dtObject; NULL when
 * the source has no transparency.  BmImage_LoadScaled's BMP round-trip
 * (see below) does not carry this through -- BMP has no such channel, and
 * no current caller needs it.
 *
 * BmImage_LoadScaled pipeline -- bypasses PDTM_SCALE (known OS bugs) *and*
 * a source-less in-memory picture.datatype object (doesn't work on OS3):
 *
 *   1. bmimage_scale_read_source_rgb() opens the source file forced to
 *      truecolor (PDTA_DestMode=PMODE_V43, PDTA_Remap=FALSE) and reads its
 *      native-size pixels into an AllocVec'd PBPAFMT_RGB (3 bytes/pixel)
 *      buffer via PDTM_READPIXELARRAY -- the technique in
 *      amigatests/testcase_scalepixelarray/datatypebmRGB.c.
 *   2. bmimage_halve_rgb() repeatedly box-average-halves that buffer
 *      ((A+B+C+D)>>2 per channel, no multiply) until neither axis is more
 *      than 2x the aspect-fit target size -- same pyramid pre-downscale as
 *      blit_bgra_to_rgba_HQ() in libutf8rastport/utf8rastport.c.
 *   3. bmimage_scale_rgb_nearest() resamples the (by then close-to-target)
 *      buffer in plain C (fixed-point nearest-neighbor) to the exact
 *      aspect-fit target size.
 *   4. bmimage_write_bmp() hand-writes that buffer to disk as a minimal
 *      24bpp BI_RGB Windows BMP -- a format close enough to raw RGB that no
 *      BMP library is needed, and one every picture.datatype install can
 *      read back from a real file (unlike a source-less object).
 *   5. bmimage_open_file_to_screen() opens that BMP exactly like any other
 *      image file (same code BmImage_Load uses) to get the screen-remapped
 *      bitmap.
 *
 * The BMP thumbnail is named "<filePath>.<targetWidth>x<targetHeight>.bmp"
 * and left on disk: it is itself a cache keyed by the *requested* box size
 * (known before decoding the source), so a repeat BmImage_LoadScaled() call
 * with the same path/target size skips straight to step 4. It lives next to
 * the source file, so a source path inside network_fs3e's managed cache
 * directory (fs3enet_cache.h) gets the thumbnail swept up for free whenever
 * that directory is flushed -- no separate cache bookkeeping needed here.
 *
 * NOTE on PDTM_READPIXELARRAY: datatypebmRGB.c found that DoDTMethod()/
 * DoDTMethodA() freeze the machine for this method; the generic alib
 * DoMethod() works. bmimage_scale_read_source_rgb() uses DoMethod() for
 * this reason -- do not "simplify" that back to DoDTMethod().
 */

#include "bmimage.h"
#include "rgbscale.h"

#include <string.h>
#include <stdio.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/alib.h>

#include <dos/dos.h>
#include <proto/dos.h>

#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>

#include "compilers.h"
#include "bdbprintf.h"
#include <proto/datatypes.h>

#include "friendsh3ep.h"
/* DataTypesBase is defined and opened via libraryTable in friendsh3ep.c. */
extern struct Library *DataTypesBase;

/* -------------------------------------------------------------------------- */

BOOL BmImage_Init(BmImage *img, const char *path)
{
    ULONG len;
    char *copy;

    if (!img) return FALSE;
    memset(img, 0, sizeof(*img));

    if (!path || path[0] == '\0') {
        img->error = BMIMAGE_ERR_NO_PATH;
        return FALSE;
    }

    len  = (ULONG)strlen(path) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (!copy) {
        img->error = BMIMAGE_ERR_NO_MEMORY;
        return FALSE;
    }
    strcpy(copy, path);
    img->filePath = copy;
    return TRUE;
}

void BmImage_Unload(BmImage *img)
{
    if (!img) return;
    if (img->dtObject) {
        DisposeDTObject(img->dtObject);
        img->dtObject = NULL;
    }
    img->bitmap = NULL;
    img->mask   = NULL;
    img->width  = 0;
    img->height = 0;
}

/* Shared by BmImage_Load and BmImage_LoadScaled's cache-hit/finish step:
 * opens an on-disk image file and remaps it to the screen's bitmap format
 * (or loads it raw when screen==NULL). Fills img->{dtObject,bitmap,mask,
 * width,height}. Pure "open file, decode, remap" -- no scaling involved. */
static BOOL bmimage_open_file_to_screen(BmImage *img, const char *path, struct Screen *screen)
{
    Object              *dto  = NULL;
    struct BitMapHeader *bmhd = NULL;
    struct BitMap       *bm   = NULL;

    bdbprintf_now("bmimage: bmimage_open_file_to_screen entering NewDTObject path=%s screen=%08lx\n",
                  path ? path : "?", (unsigned long)screen);

    if (screen) {
        ULONG depth = GetBitMapAttr(screen->RastPort.BitMap, BMA_DEPTH);
        if (depth <= 8) {
            /* indexed palette, need remap */
            dto = NewDTObject((APTR)path,
                DTA_GroupID,           GID_PICTURE,
                PDTA_Screen,           (ULONG)screen,
                PDTA_Remap,            TRUE,
                PDTA_FreeSourceBitMap, TRUE,
                TAG_DONE);
        } else {
            /* let's try to keep truecolor */
            dto = NewDTObject((APTR)path,
                DTA_GroupID,             GID_PICTURE,
                PDTA_Screen,             (ULONG)screen,
                PDTA_Remap,              FALSE,
                PDTA_DestMode,           PMODE_V43, // me want 24b, else remaped to 8.
                PDTA_SubClassRendersAll, TRUE, //  avoid one clean
                TAG_DONE);
        }
    } else {
        dto = NewDTObject((APTR)path,
            DTA_GroupID, GID_PICTURE,
            PDTA_Remap,  FALSE,
            TAG_DONE);
    }

    bdbprintf_now("bmimage: bmimage_open_file_to_screen NewDTObject done path=%s dto=%08lx\n",
                  path ? path : "?", (unsigned long)dto);
    if (!dto) {
        img->error = BMIMAGE_ERR_OPEN_FAILED;
        return FALSE;
    }

    /* Decode image and perform colour remapping on the calling process.
     * alib's DoMethod(), not DoDTMethod() -- see the file header comment:
     * DoDTMethod()/DoDTMethodA() are already known to freeze for
     * PDTM_READPIXELARRAY on this codebase's picture.datatype, and
     * DTM_PROCLAYOUT ("same as GM_LAYOUT") goes through the exact same
     * BOOPSI dispatch mechanism, just on a different method -- no reason
     * to trust the tag-call path here when the plain one is already
     * proven safe elsewhere in this file. gpLayout's fields after
     * MethodID are gpl_GInfo (NULL, no window/gadget context here) and
     * gpl_Initial (TRUE, matching the original DoDTMethod call). */
    bdbprintf_now("bmimage: entering DTM_PROCLAYOUT path=%s dto=%08lx\n",
                  path ? path : "?", (unsigned long)dto);
    DoMethod(dto, DTM_PROCLAYOUT, (ULONG)NULL, (ULONG)TRUE);
    bdbprintf_now("bmimage: DTM_PROCLAYOUT done path=%s dto=%08lx\n",
                  path ? path : "?", (unsigned long)dto);

    GetDTAttrs(dto, PDTA_BitMapHeader, (ULONG)&bmhd, TAG_DONE);
    if (bmhd) {
        img->width  = bmhd->bmh_Width;
        img->height = bmhd->bmh_Height;
    }

    /* Prefer the screen-remapped bitmap; fall back to raw source bitmap. */
    GetDTAttrs(dto, PDTA_DestBitMap, (ULONG)&bm, TAG_DONE);
    if (!bm) {
        GetDTAttrs(dto, PDTA_BitMap, (ULONG)&bm, TAG_DONE);
    }
    if (!bm) {
        DisposeDTObject(dto);
        img->error = BMIMAGE_ERR_NO_BITMAP;
        return FALSE;
    }

    img->dtObject = dto;
    img->bitmap   = bm;

    /* Transparency mask, if the source declares a transparent colour or
     * alpha channel (e.g. PNG palette index 0). One bit plane, same
     * bmh_Width/bmh_Height as the picture; NULL if there is none. */
    img->mask = NULL;
    GetDTAttrs(dto, PDTA_MaskPlane, (ULONG)&img->mask, TAG_DONE);

    img->error = BMIMAGE_OK;
    return TRUE;
}

BOOL BmImage_Load(BmImage *img, struct Screen *screen)
{
    if (!img) return FALSE;

    if (!img->filePath || img->filePath[0] == '\0') {
        img->error = BMIMAGE_ERR_NO_PATH;
        return FALSE;
    }

    if (!DataTypesBase) {
        img->error = BMIMAGE_ERR_NO_DATATYPES;
        return FALSE;
    }

    BmImage_Unload(img);

    return bmimage_open_file_to_screen(img, img->filePath, screen);
}

/* -------------------------------------------------------------------------- */
/* BmImage_LoadScaled helpers -- see the pipeline description in the file
 * header comment above. */

/* Opens filePath as a truecolor (non-remapped) picture.datatype object,
 * reads its native-size pixels into a freshly AllocVec'd RGB24 buffer, and
 * disposes the object again -- the source file itself is all that's kept
 * around (its path), no datatype object survives this call. */
static BOOL bmimage_scale_read_source_rgb(BmImage *img,
                                           UBYTE **outBuf, ULONG *outW, ULONG *outH)
{
    Object              *dto  = NULL;
    struct BitMapHeader *bmhd = NULL;
    UBYTE               *buf  = NULL;
    ULONG                w, h, rowBytes, nbpix;

    bdbprintf_now("bmimage: entering NewDTObject path=%s\n",
                  img->filePath ? img->filePath : "?");
    dto = NewDTObject((APTR)img->filePath,
        DTA_GroupID,             GID_PICTURE,
        PDTA_Remap,              FALSE,
        PDTA_DestMode,           PMODE_V43,
        PDTA_SubClassRendersAll, TRUE,
        TAG_DONE);
    bdbprintf_now("bmimage: NewDTObject done path=%s dto=%08lx\n",
                  img->filePath ? img->filePath : "?", (unsigned long)dto);
    if (!dto) {
        img->error = BMIMAGE_ERR_OPEN_FAILED;
        return FALSE;
    }

    GetDTAttrs(dto, PDTA_BitMapHeader, (ULONG)&bmhd, TAG_DONE);
    if (!bmhd || bmhd->bmh_Width < 1 || bmhd->bmh_Height < 1) {
        DisposeDTObject(dto);
        img->error = BMIMAGE_ERR_NO_BITMAP;
        return FALSE;
    }
    w = bmhd->bmh_Width;
    h = bmhd->bmh_Height;
    rowBytes = w * 3;

    buf = (UBYTE *)AllocVec(rowBytes * h, MEMF_ANY);
    if (!buf) {
        DisposeDTObject(dto);
        img->error = BMIMAGE_ERR_NO_MEMORY;
        return FALSE;
    }

    /* alib's DoMethod() -- DoDTMethod()/DoDTMethodA() freeze for this method. */
    bdbprintf_now("bmimage: entering PDTM_READPIXELARRAY path=%s w=%lu h=%lu\n",
                  img->filePath ? img->filePath : "?",
                  (unsigned long)w, (unsigned long)h);
    nbpix = DoMethod(dto,
            PDTM_READPIXELARRAY,
            (ULONG)buf, PBPAFMT_RGB, rowBytes,
            0, 0, w, h,
            TAG_DONE);
    bdbprintf_now("bmimage: PDTM_READPIXELARRAY done path=%s nbpix=%lu\n",
                  img->filePath ? img->filePath : "?", (unsigned long)nbpix);
    DisposeDTObject(dto);
    if (nbpix == 0) {
        FreeVec(buf);
        img->error = BMIMAGE_ERR_NO_BITMAP;
        return FALSE;
    }

    *outBuf = buf;
    *outW   = w;
    *outH   = h;
    return TRUE;
}

#define BMIMAGE_BMP_HEADER_SIZE 54  /* BITMAPFILEHEADER(14) + BITMAPINFOHEADER(40) */

static void bmimage_put_u16le(UBYTE *p, UWORD v)
{
    p[0] = (UBYTE)(v & 0xFF);
    p[1] = (UBYTE)((v >> 8) & 0xFF);
}

static void bmimage_put_u32le(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v & 0xFF);
    p[1] = (UBYTE)((v >> 8) & 0xFF);
    p[2] = (UBYTE)((v >> 16) & 0xFF);
    p[3] = (UBYTE)((v >> 24) & 0xFF);
}

/* Writes rgbBuf (top-down RGB24, w*h pixels) to path as a minimal 24bpp
 * BI_RGB Windows BMP: BITMAPFILEHEADER+BITMAPINFOHEADER, pixel rows
 * bottom-up, BGR pixel order, each row padded to a 4-byte boundary -- the
 * most universally-supported BMP variant. Every multi-byte header field is
 * written little-endian by hand, since this target is big-endian 68k. */
static BOOL bmimage_write_bmp(const char *path, const UBYTE *rgbBuf, ULONG w, ULONG h)
{
    UBYTE  header[BMIMAGE_BMP_HEADER_SIZE];
    ULONG  rowBytes   = w * 3;
    ULONG  paddedRow  = (rowBytes + 3) & ~3UL;
    ULONG  padLen     = paddedRow - rowBytes;
    ULONG  pixelBytes = paddedRow * h;
    ULONG  fileSize   = BMIMAGE_BMP_HEADER_SIZE + pixelBytes;
    BPTR   fh;
    UBYTE *row;
    ULONG  y, x;
    BOOL   ok = TRUE;

    row = (UBYTE *)AllocVec(paddedRow, MEMF_ANY);
    if (!row) return FALSE;

    memset(header, 0, sizeof(header));
    header[0] = 'B';
    header[1] = 'M';
    bmimage_put_u32le(header + 2,  fileSize);
    bmimage_put_u32le(header + 6,  0);                       /* reserved */
    bmimage_put_u32le(header + 10, BMIMAGE_BMP_HEADER_SIZE);  /* bfOffBits */
    bmimage_put_u32le(header + 14, 40);                       /* biSize */
    bmimage_put_u32le(header + 18, w);                        /* biWidth */
    bmimage_put_u32le(header + 22, h);                        /* biHeight (positive = bottom-up) */
    bmimage_put_u16le(header + 26, 1);                        /* biPlanes */
    bmimage_put_u16le(header + 28, 24);                       /* biBitCount */
    bmimage_put_u32le(header + 30, 0);                        /* biCompression = BI_RGB */
    bmimage_put_u32le(header + 34, pixelBytes);                /* biSizeImage */
    /* biX/YPelsPerMeter, biClrUsed, biClrImportant left 0 */

    fh = Open((STRPTR)path, MODE_NEWFILE);
    if (!fh) {
        FreeVec(row);
        return FALSE;
    }

    if (Write(fh, header, BMIMAGE_BMP_HEADER_SIZE) != BMIMAGE_BMP_HEADER_SIZE)
        ok = FALSE;

    for (y = 0; ok && y < h; y++) {
        const UBYTE *srow = rgbBuf + (h - 1 - y) * rowBytes; /* bottom-up */
        UBYTE *dp = row;

        for (x = 0; x < w; x++) {
            dp[0] = srow[x * 3 + 2]; /* B */
            dp[1] = srow[x * 3 + 1]; /* G */
            dp[2] = srow[x * 3 + 0]; /* R */
            dp += 3;
        }
        if (padLen) memset(dp, 0, padLen);

        if (Write(fh, row, (LONG)paddedRow) != (LONG)paddedRow) ok = FALSE;
    }

    Close(fh);
    FreeVec(row);
    if (!ok) DeleteFile((STRPTR)path);
    return ok;
}

BOOL BmImage_GenerateScaledBmp(const char *srcPath, const char *cacheKeyPath,
                                UWORD targetWidth, UWORD targetHeight,
                                char *outThumbPath, ULONG outPathSize, BmImageError *outError)
{
    BmImage tmp;
    UBYTE  *srcBuf = NULL;
    UBYTE  *dstBuf = NULL;
    ULONG   origW, origH, dstW, dstH;
    BPTR    probe;
    const char *keyPath = (cacheKeyPath && cacheKeyPath[0]) ? cacheKeyPath : srcPath;

    if (outError) *outError = BMIMAGE_OK;

    if (!srcPath || srcPath[0] == '\0' || !outThumbPath || outPathSize < 1) {
        if (outError) *outError = BMIMAGE_ERR_NO_PATH;
        return FALSE;
    }

    if (!DataTypesBase) {
        if (outError) *outError = BMIMAGE_ERR_NO_DATATYPES;
        return FALSE;
    }

    if (targetWidth < 1 || targetHeight < 1) {
        if (outError) *outError = BMIMAGE_ERR_NO_BITMAP;
        return FALSE;
    }

    /* Thumbnail is a sibling BMP file keyed off keyPath (usually srcPath
     * itself; see the cacheKeyPath doc comment in bmimage.h for when it
     * isn't) and the *requested* box size -- deterministic without
     * decoding the source first. Cache hit: skip the whole decode/scale/
     * write dance, srcPath doesn't even need to still exist. */
    snprintf(outThumbPath, outPathSize, "%s.%ldx%ld.bmp",
             keyPath, (long)targetWidth, (long)targetHeight);

    probe = Open((STRPTR)outThumbPath, MODE_OLDFILE);
    if (probe) {
        Close(probe);
        return TRUE;
    }

    memset(&tmp, 0, sizeof(tmp));
    tmp.filePath = (char *)srcPath; /* read-only use; not owned/freed here */

    if (!bmimage_scale_read_source_rgb(&tmp, &srcBuf, &origW, &origH)) {
        if (outError) *outError = tmp.error;
        return FALSE;
    }

    /* Fit origW x origH inside targetWidth x targetHeight, preserving aspect
     * ratio: whichever axis would overshoot the box first is clamped to the
     * box, the other axis follows the same ratio (touches its border only
     * if the aspect matches exactly). */
    if (origW * (ULONG)targetHeight > origH * (ULONG)targetWidth) {
        dstW = targetWidth;
        dstH = (origH * targetWidth) / origW;
    } else {
        dstH = targetHeight;
        dstW = (origW * targetHeight) / origH;
    }
    if (dstW < 1) dstW = 1;
    if (dstH < 1) dstH = 1;

    dstBuf = (UBYTE *)AllocVec(dstW * 3 * dstH, MEMF_ANY);
    if (!dstBuf) {
        FreeVec(srcBuf);
        if (outError) *outError = BMIMAGE_ERR_NO_MEMORY;
        return FALSE;
    }

    /* Pyramid pre-downscale + final resample, quality per
     * app->settings.scalingQuality (fs3esettingsview.c's "Thumbnails &
     * icons" group) -- see rgbscale.h's RgbScale_ToSize() doc comment. */
    RgbScale_ToSize(srcBuf, origW, origH, dstBuf, dstW, dstH,
                     app ? app->settings.scalingQuality : FS3E_SCALEQ_BILINEAR);
    FreeVec(srcBuf);

    if (!bmimage_write_bmp(outThumbPath, dstBuf, dstW, dstH)) {
        FreeVec(dstBuf);
        if (outError) *outError = BMIMAGE_ERR_WRITE_FAILED;
        return FALSE;
    }
    FreeVec(dstBuf);

    return TRUE;
}

BmImageFormat BmImage_SniffFormat(const char *path)
{
    BPTR  fh;
    UBYTE buf[16];
    LONG  n;
    BmImageFormat fmt = BMFMT_UNKNOWN;

    if (!path || !path[0]) return BMFMT_UNKNOWN;

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) return BMFMT_UNKNOWN;
    n = Read(fh, buf, sizeof(buf));
    Close(fh);
    if (n < 4) return BMFMT_UNKNOWN;

    if (n >= 8 && memcmp(buf, "\x89PNG\r\n\x1a\n", 8) == 0)
        fmt = BMFMT_PNG;
    else if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
        fmt = BMFMT_JPEG;
    else if (n >= 6 && (memcmp(buf, "GIF87a", 6) == 0 || memcmp(buf, "GIF89a", 6) == 0))
        fmt = BMFMT_GIF;
    else if (n >= 12 && memcmp(buf, "RIFF", 4) == 0 && memcmp(buf + 8, "WEBP", 4) == 0)
        fmt = BMFMT_WEBP;
    else if (n >= 2 && buf[0] == 'B' && buf[1] == 'M')
        fmt = BMFMT_BMP;

    return fmt;
}

BOOL BmImage_LoadScaled(BmImage *img, struct Screen *screen,
                         UWORD targetWidth, UWORD targetHeight)
{
    char        thumbPath[320];
    BmImageError genErr = BMIMAGE_OK;

    if (!img) return FALSE;

    if (!img->filePath || img->filePath[0] == '\0') {
        img->error = BMIMAGE_ERR_NO_PATH;
        return FALSE;
    }

    if (!DataTypesBase) {
        img->error = BMIMAGE_ERR_NO_DATATYPES;
        return FALSE;
    }

    if (targetWidth < 1 || targetHeight < 1) {
        img->error = BMIMAGE_ERR_NO_BITMAP;
        return FALSE;
    }

    BmImage_Unload(img);

    if (!BmImage_GenerateScaledBmp(img->filePath, NULL, targetWidth, targetHeight,
                                    thumbPath, sizeof(thumbPath), &genErr)) {
        img->error = genErr;
        return FALSE;
    }

    return bmimage_open_file_to_screen(img, thumbPath, screen);
}

void BmImage_Free(BmImage *img)
{
    if (!img) return;
    BmImage_Unload(img);
    if (img->filePath) {
        FreeVec(img->filePath);
        img->filePath = NULL;
    }
}

BOOL BmImage_IsLoaded(const BmImage *img)
{
    return (BOOL)(img && img->bitmap != NULL);
}
