/*
 * avatarimages.c - GUI-side avatar bitmap cache for FriendSh3ep.
 */

#include "avatarimages.h"
#include <proto/exec.h>
#include <string.h>

AvatarImages *AvatarImages_Create(void)
{
    return (AvatarImages *)AllocVec(sizeof(AvatarImages), MEMF_ANY | MEMF_CLEAR);
}

void AvatarImages_Dispose(AvatarImages *ai)
{
    ULONG i;
    if (!ai) return;
    for (i = 0; i < ai->count; i++)
        RgbImage_Free(&ai->entries[i].img);
    for (i = 0; i < ai->thumbCount; i++)
        RgbImage_Free(&ai->thumbs[i].img);
    FreeVec(ai);
}

static AvatarEntry *find_entry(AvatarImages *ai, const char *acct)
{
    ULONG i;
    if (!ai || !acct || !acct[0]) return NULL;
    for (i = 0; i < ai->count; i++)
        if (strncmp(ai->entries[i].acct, acct, AVATAR_ACCT_SIZE - 1) == 0)
            return &ai->entries[i];
    return NULL;
}

static AvatarEntry *find_or_create(AvatarImages *ai, const char *acct)
{
    AvatarEntry *e = find_entry(ai, acct);
    if (e) return e;
    if (!ai || !acct || !acct[0]) return NULL;

    if (ai->count < AVATAR_CACHE_MAX) {
        e = &ai->entries[ai->count++];
    } else {
        /* Round-robin eviction once full -- same reasoning as
         * find_or_create_media below: never evict a slot with a fetch/
         * thumbnail still outstanding (requested but not yet loaded and
         * not failed), since that would let a later timeline refresh
         * re-request the same acct and race a second fetch/thumbnail
         * chain against the first one still in flight. A failed entry
         * has nothing in flight for it, so it's just as evictable as a
         * loaded one. Search up to a full lap for an idle slot; if every
         * slot is genuinely in flight, decline to track this acct this
         * round rather than evict something live. */
        ULONG tries;
        AvatarEntry *victim = NULL;
        for (tries = 0; tries < AVATAR_CACHE_MAX; tries++) {
            ULONG idx = (ai->avatarNextEvict + tries) % AVATAR_CACHE_MAX;
            AvatarEntry *cand = &ai->entries[idx];
            if (!cand->requested || RgbImage_IsLoaded(&cand->img) || cand->failed) {
                victim = cand;
                ai->avatarNextEvict = (idx + 1) % AVATAR_CACHE_MAX;
                break;
            }
        }
        if (!victim) return NULL;
        e = victim;
        RgbImage_Free(&e->img);
    }
    memset(e, 0, sizeof(*e));
    strncpy(e->acct, acct, AVATAR_ACCT_SIZE - 1);
    return e;
}

RgbImage *AvatarImages_Get(AvatarImages *ai, const char *acct)
{
    AvatarEntry *e = find_entry(ai, acct);
    if (!e || !RgbImage_IsLoaded(&e->img)) return NULL;
    return &e->img;
}

BOOL AvatarImages_IsRequested(AvatarImages *ai, const char *acct)
{
    AvatarEntry *e = find_entry(ai, acct);
    return (e && e->requested) ? TRUE : FALSE;
}

void AvatarImages_MarkRequested(AvatarImages *ai, const char *acct)
{
    AvatarEntry *e = find_or_create(ai, acct);
    if (e) e->requested = TRUE;
}

BOOL AvatarImages_IsThumbRequested(AvatarImages *ai, const char *acct)
{
    AvatarEntry *e = find_entry(ai, acct);
    return (e && e->thumbRequested) ? TRUE : FALSE;
}

void AvatarImages_MarkThumbRequested(AvatarImages *ai, const char *acct)
{
    AvatarEntry *e = find_or_create(ai, acct);
    if (e) e->thumbRequested = TRUE;
}

RgbImage *AvatarImages_ThumbReady(AvatarImages *ai, const char *acct,
                                   const char *thumbPath)
{
    AvatarEntry *e;

    if (!ai || !acct || !acct[0] || !thumbPath || !thumbPath[0]) return NULL;

    e = find_or_create(ai, acct);
    if (!e) return NULL;

    if (!RgbImage_LoadBmp(&e->img, thumbPath)) return NULL;

    return &e->img;
}

void AvatarImages_MarkFailed(AvatarImages *ai, const char *acct, UBYTE detectedFormat)
{
    AvatarEntry *e = find_or_create(ai, acct);
    if (!e) return;
    e->failed         = TRUE;
    e->detectedFormat = detectedFormat;
}

BOOL AvatarImages_Failed(AvatarImages *ai, const char *acct, UBYTE *outFormat)
{
    AvatarEntry *e = find_entry(ai, acct);
    if (!e || !e->failed) return FALSE;
    if (outFormat) *outFormat = e->detectedFormat;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Media thumbnail pool -- see the file header comment for why this one  */
/* is smaller and round-robin evicted instead of ever-growing like the  */
/* avatar pool above.                                                    */
/* ------------------------------------------------------------------ */

static ThumbnailEntry *find_media_entry(AvatarImages *ai, const char *url)
{
    ULONG i;
    if (!ai || !url || !url[0]) return NULL;
    for (i = 0; i < ai->thumbCount; i++)
        if (strncmp(ai->thumbs[i].url, url, THUMBNAIL_URL_SIZE - 1) == 0)
            return &ai->thumbs[i];
    return NULL;
}

static ThumbnailEntry *find_or_create_media(AvatarImages *ai, const char *url)
{
    ThumbnailEntry *e = find_media_entry(ai, url);
    if (e) return e;
    if (!ai || !url || !url[0]) return NULL;

    if (ai->thumbCount < THUMBNAIL_CACHE_MAX) {
        e = &ai->thumbs[ai->thumbCount++];
    } else {
        /* Round-robin eviction, but never a slot with a fetch/thumbnail
         * still outstanding (requested but not yet RgbImage_IsLoaded and
         * not failed) -- evicting one of those would reset its
         * .requested/.thumbRequested flags while the network/thumbnail
         * process is still mid-flight on the RAM:T file that entry's url
         * maps to, letting a later timeline refresh re-request the same
         * url and start a *second*, independent fetch/thumbnail chain
         * that ends up racing the first one over that same file. A
         * failed entry has nothing in flight for it anymore, so it's
         * just as evictable as a loaded one. Search up to a full lap for
         * a slot that's actually idle; if every slot is genuinely in
         * flight (all 32 at once), decline to track this url this round
         * rather than evict something live -- it'll just get tried again
         * on the next pass. */
        ULONG tries;
        ThumbnailEntry *victim = NULL;
        for (tries = 0; tries < THUMBNAIL_CACHE_MAX; tries++) {
            ULONG idx = (ai->thumbNextEvict + tries) % THUMBNAIL_CACHE_MAX;
            ThumbnailEntry *cand = &ai->thumbs[idx];
            if (!cand->requested || RgbImage_IsLoaded(&cand->img) || cand->failed) {
                victim = cand;
                ai->thumbNextEvict = (idx + 1) % THUMBNAIL_CACHE_MAX;
                break;
            }
        }
        if (!victim) return NULL;
        e = victim;
        RgbImage_Free(&e->img);
    }
    memset(e, 0, sizeof(*e));
    strncpy(e->url, url, THUMBNAIL_URL_SIZE - 1);
    return e;
}

RgbImage *AvatarImages_GetMedia(AvatarImages *ai, const char *url)
{
    ThumbnailEntry *e = find_media_entry(ai, url);
    if (!e || !RgbImage_IsLoaded(&e->img)) return NULL;
    return &e->img;
}

BOOL AvatarImages_IsMediaRequested(AvatarImages *ai, const char *url)
{
    ThumbnailEntry *e = find_media_entry(ai, url);
    return (e && e->requested) ? TRUE : FALSE;
}

void AvatarImages_MarkMediaRequested(AvatarImages *ai, const char *url)
{
    ThumbnailEntry *e = find_or_create_media(ai, url);
    if (e) e->requested = TRUE;
}

BOOL AvatarImages_IsMediaThumbRequested(AvatarImages *ai, const char *url)
{
    ThumbnailEntry *e = find_media_entry(ai, url);
    return (e && e->thumbRequested) ? TRUE : FALSE;
}

void AvatarImages_MarkMediaThumbRequested(AvatarImages *ai, const char *url)
{
    ThumbnailEntry *e = find_or_create_media(ai, url);
    if (e) e->thumbRequested = TRUE;
}

RgbImage *AvatarImages_MediaThumbReady(AvatarImages *ai, const char *url,
                                        const char *thumbPath)
{
    ThumbnailEntry *e;

    if (!ai || !url || !url[0] || !thumbPath || !thumbPath[0]) return NULL;

    e = find_or_create_media(ai, url);
    if (!e) return NULL;

    if (!RgbImage_LoadBmp(&e->img, thumbPath)) return NULL;

    return &e->img;
}

void AvatarImages_MarkMediaFailed(AvatarImages *ai, const char *url, UBYTE detectedFormat)
{
    ThumbnailEntry *e = find_or_create_media(ai, url);
    if (!e) return;
    e->failed         = TRUE;
    e->detectedFormat = detectedFormat;
}

BOOL AvatarImages_MediaFailed(AvatarImages *ai, const char *url, UBYTE *outFormat)
{
    ThumbnailEntry *e = find_media_entry(ai, url);
    if (!e || !e->failed) return FALSE;
    if (outFormat) *outFormat = e->detectedFormat;
    return TRUE;
}
