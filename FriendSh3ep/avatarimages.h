/*
 * avatarimages.h - GUI-side avatar bitmap cache for FriendSh3ep.
 *
 * Maintains one BmImage per unique @user@instance, scaled to the current
 * avatarSize.  All bitmaps must be closed on iconify and reloaded when the
 * display returns.  When avatarSize changes (font/DPI adjustment) call
 * AvatarImages_Reload() with the new size to rescale everything.
 *
 * Download flow:
 *   1. Timeline arrives → for each post whose acct is not yet requested,
 *      send FS3ENETQ_FETCH_IMAGE(url, key=acct) to the network process.
 *   2. Network process checks disk cache, downloads on miss, replies with
 *      the local file path.
 *   3. GUI calls AvatarImages_GotFile(key, localPath, screen, size).
 *   4. Tile renderer calls AvatarImages_Get(acct) and blits the bitmap.
 */

#ifndef AVATARIMAGES_H
#define AVATARIMAGES_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "bmimage.h"

#define AVATAR_CACHE_MAX  128   /* max unique users kept in memory */
#define AVATAR_ACCT_SIZE  128   /* max @user@instance length + NUL */
#define AVATAR_PATH_SIZE  256   /* max disk-cache path length + NUL */

typedef struct {
    char    acct[AVATAR_ACCT_SIZE]; /* key: @user@instance */
    char    filePath[AVATAR_PATH_SIZE]; /* disk path; "" = not on disk yet */
    BmImage bm;                     /* scaled bitmap (may be unloaded) */
    BOOL    requested;              /* FETCH_IMAGE sent, reply pending */
} AvatarEntry;

typedef struct AvatarImages {
    AvatarEntry entries[AVATAR_CACHE_MAX];
    ULONG       count;
} AvatarImages;

/* Allocate the cache.  Returns NULL on memory failure. */
AvatarImages *AvatarImages_Create(void);

/* Unload all bitmaps and free the cache struct. */
void          AvatarImages_Dispose(AvatarImages *ai);

/* Return the loaded BmImage for acct, or NULL if not loaded yet. */
BmImage      *AvatarImages_Get(AvatarImages *ai, const char *acct);

/* TRUE if a FETCH_IMAGE request has already been sent for this acct. */
BOOL          AvatarImages_IsRequested(AvatarImages *ai, const char *acct);

/* Record that a FETCH_IMAGE was sent for acct (prevents double-fetch). */
void          AvatarImages_MarkRequested(AvatarImages *ai, const char *acct);

/* Called when the network process replies with a downloaded file.
 * Loads and scales the bitmap; returns the BmImage or NULL on failure. */
BmImage      *AvatarImages_GotFile(AvatarImages *ai, const char *acct,
                                    const char *filePath,
                                    struct Screen *screen, UWORD size);

/* Unload all bitmaps (iconify / screen close).  File paths are kept. */
void          AvatarImages_Unload(AvatarImages *ai);

/* Reload all bitmaps that have a file on disk.  Pass the new avatarSize
 * so that a font/DPI change rescales all avatars at once. */
void          AvatarImages_Reload(AvatarImages *ai,
                                   struct Screen *screen, UWORD size);

#endif /* AVATARIMAGES_H */
