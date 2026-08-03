/*
 * avatarimages.h - GUI-side image cache for FriendSh3ep.
 *
 * One AvatarImages instance manages TWO pools sharing the same underlying
 * mechanism and download/thumbnail pipeline, but tuned differently since
 * their keys have very different cardinality:
 *
 *   - Avatars: one RgbImage per unique @user@instance seen, keyed by acct.
 *     AVATAR_CACHE_MAX is generous since unique users in a session rarely
 *     get close to it, but a long enough session scrolling a busy home/
 *     federated timeline does eventually reach it -- confirmed as the
 *     cause of avatars silently, permanently never loading past that
 *     point (find_or_create used to just return NULL once full, with no
 *     eviction at all). Round-robin evicted the same way as media
 *     thumbnails below now (see find_or_create in the .c).
 *   - Media thumbnails: one RgbImage per unique attachment preview URL,
 *     keyed by that URL. Far more numerous over a long scroll session (a
 *     handful of distinct users vs. potentially hundreds of distinct
 *     images), so THUMBNAIL_CACHE_MAX is smaller and round-robin evicted
 *     (see AvatarImages_MarkMediaRequested/find_or_create_media in the
 *     .c) -- fine, since a post scrolled far enough away to need its
 *     thumbnail re-fetched has also had its TootTimeline tile/hot-spot
 *     state recycled by then anyway.
 *
 * Both pools are plain Fast-RAM RGB pixel arrays (see rgbimage.h) with no
 * screen-bound resource and no per-DPI variant: *_Get()'s result is
 * box-fit-scaled to whatever size the caller needs at *draw* time
 * (RgbImage_DrawScaled), so a font/DPI change or an iconify/uniconify
 * cycle needs no reload, unload, or rescale here at all -- see
 * PlanToReworkThumbnails.txt steps 2-3.
 *
 * Download flow (see fs3ethumb.h for the thumbnail process this relies on
 * to keep the GUI task from freezing on a large image decode), avatars
 * shown, media thumbnails in parentheses where they differ:
 *   1. Timeline arrives → for each post whose acct (media URL) is not yet
 *      requested, send FS3ENETQ_FETCH_IMAGE(url, key=acct, subdir=
 *      "usericons" ("thumbnails")) to the network process.
 *   2. Network process checks disk cache, downloads on miss, replies with
 *      the local (possibly large, original-size) file path.
 *   3. GUI marks the acct/URL thumb-requested (AvatarImages_
 *      MarkThumbRequested/MarkMediaThumbRequested) and sends
 *      FS3EThumb_Request(path, key, kind, 64,64 (200,600)) to the
 *      thumbnail process -- the expensive decode+scale happens off the
 *      GUI task.
 *   4. Thumbnail process replies with a small, already-scaled BMP path.
 *      GUI calls AvatarImages_ThumbReady/MediaThumbReady(key, thumbPath):
 *      a cheap direct read of that small file's pixels (RgbImage_
 *      LoadBmp), no datatype decode and no scaling.
 *   5. Tile renderer calls AvatarImages_Get/GetMedia(key) and draws it
 *      with RgbImage_DrawScaled() at whatever size the tile needs.
 */

#ifndef AVATARIMAGES_H
#define AVATARIMAGES_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "rgbimage.h"

#define AVATAR_CACHE_MAX  128   /* max unique users kept in memory */
#define AVATAR_ACCT_SIZE  128   /* max @user@instance length + NUL */

/* Media URLs run much longer than accts (signed CDN query params) and the
 * pool is round-robin evicted rather than ever-growing, so it doesn't need
 * anywhere near AVATAR_CACHE_MAX slots -- see the file header comment. */
#define THUMBNAIL_CACHE_MAX  32
#define THUMBNAIL_URL_SIZE   384

/* Link-preview card images: a THIRD pool, same shape/reasoning as the media
 * thumbnail pool above, but kept separate (own subdir -- see
 * FS3E_CACHE_SUBDIR_CARDIMAGES in fs3enet.h -- own cache pool here) so a
 * card's image URL can never collide with or evict an actual attachment's
 * cache entry, even though both are downloaded/decoded through the
 * identical pipeline. */
#define CARDIMAGE_CACHE_MAX  16
#define CARDIMAGE_URL_SIZE   384

typedef struct {
    char     acct[AVATAR_ACCT_SIZE]; /* key: @user@instance */
    RgbImage img;                    /* fixed-size RGB pixel buffer */
    BOOL     requested;              /* FETCH_IMAGE sent, reply pending */
    BOOL     thumbRequested;         /* FS3ETHUMBQ_MAKE sent, reply pending */
    BOOL     failed;                 /* thumbnail process replied FS3ETHUMBR_ERROR --
                                       * distinct from "still pending" (requested==TRUE,
                                       * failed==FALSE, img not loaded); see
                                       * AvatarImages_MarkFailed. */
    UBYTE    detectedFormat;         /* enum BmImageFormat (bmimage.h), meaningful only
                                       * when failed==TRUE; BMFMT_UNKNOWN otherwise. */
} AvatarEntry;

typedef struct {
    char     url[THUMBNAIL_URL_SIZE]; /* key: attachment preview/URL */
    RgbImage img;
    BOOL     requested;
    BOOL     thumbRequested;
    BOOL     failed;
    UBYTE    detectedFormat;
} ThumbnailEntry;

typedef struct {
    char     url[CARDIMAGE_URL_SIZE]; /* key: card.image URL */
    RgbImage img;
    BOOL     requested;
    BOOL     thumbRequested;
    BOOL     failed;
    UBYTE    detectedFormat;
} CardImageEntry;

typedef struct AvatarImages {
    AvatarEntry entries[AVATAR_CACHE_MAX];
    ULONG       count;            /* populated slots, <=AVATAR_CACHE_MAX */
    ULONG       avatarNextEvict;  /* round-robin cursor once full */

    ThumbnailEntry thumbs[THUMBNAIL_CACHE_MAX];
    ULONG          thumbCount;       /* populated slots, <=THUMBNAIL_CACHE_MAX */
    ULONG          thumbNextEvict;   /* round-robin cursor once full */

    CardImageEntry cards[CARDIMAGE_CACHE_MAX];
    ULONG          cardCount;        /* populated slots, <=CARDIMAGE_CACHE_MAX */
    ULONG          cardNextEvict;    /* round-robin cursor once full */
} AvatarImages;

/* Allocate the cache.  Returns NULL on memory failure. */
AvatarImages *AvatarImages_Create(void);

/* Frees all pixel buffers (both pools) and the cache struct. */
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

/* Called when the thumbnail process replies FS3ETHUMBR_ERROR instead --
 * latches failed distinctly from "still pending" (see the AvatarEntry
 * field comments) so it's never mistaken for in-flight and never redrawn
 * as a bare "still loading" placeholder forever. detectedFormat is
 * BmImage_SniffFormat()'s result (BMFMT_UNKNOWN if not determined). */
void          AvatarImages_MarkFailed(AvatarImages *ai, const char *acct,
                                       UBYTE detectedFormat);

/* TRUE if acct's fetch/decode failed (see AvatarImages_MarkFailed);
 * *outFormat receives the detected format when non-NULL. FALSE (and
 * *outFormat left untouched) if there's no entry or it didn't fail. */
BOOL          AvatarImages_Failed(AvatarImages *ai, const char *acct,
                                   UBYTE *outFormat);

/* ---- Media thumbnail pool: same shape as the avatar functions above,
 * keyed by attachment URL instead of acct -- see the file header comment
 * for why this pool is smaller and round-robin evicted. A lookup/mark
 * call can silently recycle another URL's slot; that's fine, it will just
 * be re-requested next time it's drawn. ---- */

RgbImage     *AvatarImages_GetMedia(AvatarImages *ai, const char *url);
BOOL          AvatarImages_IsMediaRequested(AvatarImages *ai, const char *url);
void          AvatarImages_MarkMediaRequested(AvatarImages *ai, const char *url);
BOOL          AvatarImages_IsMediaThumbRequested(AvatarImages *ai, const char *url);
void          AvatarImages_MarkMediaThumbRequested(AvatarImages *ai, const char *url);
/* rawOriginal FALSE (the usual case): thumbPath is a real minified BMP
 * (RgbImage_LoadBmp, cheap direct read). TRUE: thumbPath actually holds
 * the untouched original image, just saved/renamed under the same
 * deterministic name a minified thumbnail would use (fs3esettings.h's
 * minifyThumbnails FALSE) -- decoded via RgbImage_LoadViaDatatype instead
 * (rgbimage.h), a full datatype decode. */
RgbImage     *AvatarImages_MediaThumbReady(AvatarImages *ai, const char *url,
                                            const char *thumbPath, BOOL rawOriginal);
void          AvatarImages_MarkMediaFailed(AvatarImages *ai, const char *url,
                                            UBYTE detectedFormat);
BOOL          AvatarImages_MediaFailed(AvatarImages *ai, const char *url,
                                        UBYTE *outFormat);

/* ---- Card image pool: same shape again, keyed by card.image URL -- see
 * CARDIMAGE_CACHE_MAX's comment above for why it's kept separate from the
 * media pool. ---- */

RgbImage     *AvatarImages_GetCard(AvatarImages *ai, const char *url);
BOOL          AvatarImages_IsCardRequested(AvatarImages *ai, const char *url);
void          AvatarImages_MarkCardRequested(AvatarImages *ai, const char *url);
BOOL          AvatarImages_IsCardThumbRequested(AvatarImages *ai, const char *url);
void          AvatarImages_MarkCardThumbRequested(AvatarImages *ai, const char *url);
/* rawOriginal: see AvatarImages_MediaThumbReady's doc comment above. */
RgbImage     *AvatarImages_CardThumbReady(AvatarImages *ai, const char *url,
                                           const char *thumbPath, BOOL rawOriginal);
void          AvatarImages_MarkCardFailed(AvatarImages *ai, const char *url,
                                           UBYTE detectedFormat);
BOOL          AvatarImages_CardFailed(AvatarImages *ai, const char *url,
                                       UBYTE *outFormat);

#endif /* AVATARIMAGES_H */
