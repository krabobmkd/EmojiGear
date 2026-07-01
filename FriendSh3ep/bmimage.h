/*
 * bmimage.h - bitmap image loader via picture.datatype for FriendSh3ep.
 *
 * $VER: bmimage.h 1.0 (01.07.2026)
 * Copyright (C) 2026 FriendSh3ep contributors. All rights reserved.
 *
 * Loads JPEG/PNG/BMP/IFF images using datatypes.library v44+ and
 * picture.datatype, remapped to the current screen's bitmap format.
 *
 * Lifecycle:
 *   BmImage_Init()   -- once per instance, copies file path
 *   BmImage_Load()   -- on each screen open; sets bitmap pointer
 *   BmImage_Unload() -- on iconify / screen close; frees bitmap, keeps path
 *   BmImage_Free()   -- once at teardown; frees path too
 *
 * datatypes.library v44 is opened and closed via libraryTable in friendsh3ep.c.
 */

#ifndef BMIMAGE_H
#define BMIMAGE_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include <graphics/gfx.h>
#include <intuition/classusr.h>

typedef enum {
    BMIMAGE_OK             = 0,
    BMIMAGE_ERR_NO_PATH,        /* NULL or empty path given to BmImage_Init */
    BMIMAGE_ERR_NO_MEMORY,      /* AllocVec failed */
    BMIMAGE_ERR_NO_DATATYPES,   /* datatypes.library v44 not available */
    BMIMAGE_ERR_OPEN_FAILED,    /* NewDTObject returned NULL */
    BMIMAGE_ERR_NO_BITMAP       /* layout ok but no bitmap returned */
} BmImageError;

typedef struct BmImage {
    char           *filePath;   /* AllocVec'd; survives Load/Unload cycles */
    Object         *dtObject;   /* picture.datatype object; NULL when unloaded */
    struct BitMap  *bitmap;     /* owned by dtObject; valid when dtObject != NULL */
    UWORD           width;      /* pixel width from BitMapHeader */
    UWORD           height;     /* pixel height from BitMapHeader */
    BmImageError    error;      /* last error code; BMIMAGE_OK on success */
} BmImage;

/*
 * Initialise from a file path.  Duplicates the path string.
 * Does NOT load the image — call BmImage_Load() to do that.
 * Returns FALSE if path is NULL/empty or AllocVec fails.
 */
BOOL BmImage_Init(BmImage *img, const char *path);

/*
 * Load (or reload) the bitmap, remapped to screen's depth and format.
 * Pass screen=NULL to load raw (un-remapped) source bitmap instead.
 * img->bitmap is valid after TRUE return; img->error is set after FALSE.
 * If already loaded, unloads the previous bitmap first.
 */
BOOL BmImage_Load(BmImage *img, struct Screen *screen);

/*
 * Dispose the datatype object and clear bitmap/dimensions.
 * File path is kept so BmImage_Load() can be called again.
 * Call on iconify or screen close.
 */
void BmImage_Unload(BmImage *img);

/*
 * Unload bitmap and free the file path.  Leaves img zeroed.
 * Call once at final teardown.
 */
void BmImage_Free(BmImage *img);

#endif /* BMIMAGE_H */
