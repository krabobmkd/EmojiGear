/*
 * bmimage.c - bitmap image loader via picture.datatype for FriendSh3ep.
 *
 * $VER: bmimage.c 1.0 (01.07.2026)
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
 * the source has no transparency.
 */

#include "bmimage.h"

#include <string.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>

#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/pictureclass.h>
#include <proto/datatypes.h>

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

BOOL BmImage_Load(BmImage *img, struct Screen *screen)
{
    Object              *dto  = NULL;
    struct BitMapHeader *bmhd = NULL;
    struct BitMap       *bm   = NULL;

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

    if (screen) {
        ULONG depth = GetBitMapAttr( screen->RastPort.BitMap, BMA_DEPTH );
        printf(" **** screen depth:%d\n",depth);
        if(depth<=8)
        {   /* indexed palette, need remap */
            dto = NewDTObject((APTR)img->filePath,
                DTA_GroupID,           GID_PICTURE,
                PDTA_Screen,           (ULONG)screen,
                PDTA_Remap,            TRUE,
                PDTA_FreeSourceBitMap, TRUE,
                TAG_DONE);
        } else
        {
            /* let's try to keep truecolor */
            dto = NewDTObject((APTR)img->filePath,
                DTA_GroupID,           GID_PICTURE,
                PDTA_Screen,           (ULONG)screen,
                PDTA_Remap,             FALSE,
                PDTA_DestMode,          PMODE_V43, // me want 24b, else remaped to 8.
                PDTA_SubClassRendersAll, TRUE, //  avoid one clean
               // PDTA_FreeSourceBitMap, TRUE,
                TAG_DONE);
        }
    } else {
        dto = NewDTObject((APTR)img->filePath,
            DTA_GroupID, GID_PICTURE,
            PDTA_Remap,  FALSE,
            TAG_DONE);
    }

    if (!dto) {
        img->error = BMIMAGE_ERR_OPEN_FAILED;
        return FALSE;
    }

    /* Decode image and perform colour remapping on the calling process. */
    DoDTMethod(dto, NULL, NULL, DTM_PROCLAYOUT, NULL, TRUE);

    /* Read back dimensions. */
    GetDTAttrs(dto, PDTA_BitMapHeader, (ULONG)&bmhd, TAG_DONE);
    if (bmhd) {
        img->width  = bmhd->bmh_Width;
        img->height = bmhd->bmh_Height;
    }

    /* Prefer the screen-remapped bitmap; fall back to raw source bitmap. */
    GetDTAttrs(dto, PDTA_DestBitMap, (ULONG)&bm, TAG_DONE);
    if (!bm)
        GetDTAttrs(dto, PDTA_BitMap, (ULONG)&bm, TAG_DONE);

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

void BmImage_Free(BmImage *img)
{
    if (!img) return;
    BmImage_Unload(img);
    if (img->filePath) {
        FreeVec(img->filePath);
        img->filePath = NULL;
    }
}

