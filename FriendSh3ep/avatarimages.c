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
    if (!e) {
        if (!ai || ai->count >= AVATAR_CACHE_MAX) return NULL;
        e = &ai->entries[ai->count++];
        memset(e, 0, sizeof(*e));
        strncpy(e->acct, acct, AVATAR_ACCT_SIZE - 1);
    }
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
