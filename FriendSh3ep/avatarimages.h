/*
 * avatarimages.h - GUI-side avatar bitmap cache for FriendSh3ep.
 *
 * Maintains one fixed-size RgbImage per unique @user@instance (see
 * rgbimage.h). Unlike the old BmImage-based cache, this buffer is a plain
 * Fast-RAM RGB pixel array with no screen-bound resource and no per-DPI
 * variant: AvatarImages_Get()'s result is box-fit-scaled to the live
 * avatarSize at *draw* time (RgbImage_DrawScaled), so a font/DPI change or
 * an iconify/uniconify cycle needs no reload, unload, or rescale here at
 * all -- see PlanToReworkThumbnails.txt steps 2-3.
 *
 * Download flow (see fs3ethumb.h for the thumbnail process this relies on
 * to keep the GUI task from freezing on a large avatar upload):
 *   1. Timeline arrives → for each post whose acct is not yet requested,
 *      send FS3ENETQ_FETCH_IMAGE(url, key=acct) to the network process.
 *   2. Network process checks disk cache, downloads on miss, replies with
 *      the local (possibly large, original-size) file path.
 *   3. GUI marks the acct thumb-requested (AvatarImages_MarkThumbRequested)
 *      and sends FS3EThumb_Request(path, acct, 64, 64) to the thumbnail
 *      process -- the expensive decode+scale happens off the GUI task.
 *   4. Thumbnail process replies with a small, already-scaled BMP path.
 *      GUI calls AvatarImages_ThumbReady(acct, thumbPath): a cheap direct
 *      read of that small file's pixels (RgbImage_LoadBmp), no datatype
 *      decode and no scaling.
 *   5. Tile renderer calls AvatarImages_Get(acct) and draws it with
 *      RgbImage_DrawScaled() at whatever size the tile needs.
 */

#ifndef AVATARIMAGES_H
#define AVATARIMAGES_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "rgbimage.h"

#define AVATAR_CACHE_MAX  128   /* max unique users kept in memory */
#define AVATAR_ACCT_SIZE  128   /* max @user@instance length + NUL */

typedef struct {
    char     acct[AVATAR_ACCT_SIZE]; /* key: @user@instance */
    RgbImage img;                    /* fixed-size RGB pixel buffer */
    BOOL     requested;              /* FETCH_IMAGE sent, reply pending */
    BOOL     thumbRequested;         /* FS3ETHUMBQ_MAKE sent, reply pending */
} AvatarEntry;

typedef struct AvatarImages {
    AvatarEntry entries[AVATAR_CACHE_MAX];
    ULONG       count;
} AvatarImages;

/* Allocate the cache.  Returns NULL on memory failure. */
AvatarImages *AvatarImages_Create(void);

/* Frees all pixel buffers and the cache struct. */
void          AvatarImages_Dispose(AvatarImages *ai);

/* Return the loaded RgbImage for acct, or NULL if not loaded yet. */
RgbImage     *AvatarImages_Get(AvatarImages *ai, const char *acct);

/* TRUE if a FETCH_IMAGE request has already been sent for this acct. */
BOOL          AvatarImages_IsRequested(AvatarImages *ai, const char *acct);

/* Record that a FETCH_IMAGE was sent for acct (prevents double-fetch). */
void          AvatarImages_MarkRequested(AvatarImages *ai, const char *acct);

/* TRUE if a thumbnail-process request has already been sent for this acct. */
BOOL          AvatarImages_IsThumbRequested(AvatarImages *ai, const char *acct);

/* Record that an FS3ETHUMBQ_MAKE was sent for acct (prevents double-request). */
void          AvatarImages_MarkThumbRequested(AvatarImages *ai, const char *acct);

/* Called when the thumbnail process replies with a scaled-down thumbnail
 * file (see fs3ethumb.h). Loads its pixels; returns the RgbImage or NULL
 * on failure. */
RgbImage     *AvatarImages_ThumbReady(AvatarImages *ai, const char *acct,
                                       const char *thumbPath);

#endif /* AVATARIMAGES_H */
