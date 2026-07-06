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
        BmImage_Free(&ai->entries[i].bm);
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

BmImage *AvatarImages_Get(AvatarImages *ai, const char *acct)
{
    AvatarEntry *e = find_entry(ai, acct);
    if (!e || !BmImage_IsLoaded(&e->bm)) return NULL;
    return &e->bm;
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

BmImage *AvatarImages_GotFile(AvatarImages *ai, const char *acct,
                               const char *filePath,
                               struct Screen *screen, UWORD size)
{
    AvatarEntry *e;

    if (!ai || !acct || !acct[0] || !filePath || !filePath[0]) return NULL;

    e = find_or_create(ai, acct);
    if (!e) return NULL;

    strncpy(e->filePath, filePath, AVATAR_PATH_SIZE - 1);
    e->filePath[AVATAR_PATH_SIZE - 1] = '\0';

    /* Re-init so BmImage_LoadScaled picks up the (possibly new) path. */
    BmImage_Free(&e->bm);
    if (!BmImage_Init(&e->bm, filePath)) return NULL;
    if (!BmImage_LoadScaled(&e->bm, screen, size, size)) return NULL;

    return &e->bm;
}

void AvatarImages_Unload(AvatarImages *ai)
{
    ULONG i;
    if (!ai) return;
    for (i = 0; i < ai->count; i++)
        BmImage_Unload(&ai->entries[i].bm);
}

void AvatarImages_Reload(AvatarImages *ai, struct Screen *screen, UWORD size)
{
    ULONG i;
    if (!ai) return;
    for (i = 0; i < ai->count; i++) {
        AvatarEntry *e = &ai->entries[i];
        if (!e->filePath[0]) continue;
        /* BmImage_Unload keeps filePath intact; LoadScaled handles already-loaded. */
        if (!e->bm.filePath) {
            if (!BmImage_Init(&e->bm, e->filePath)) continue;
        }
        BmImage_LoadScaled(&e->bm, screen, size, size);
    }
}
