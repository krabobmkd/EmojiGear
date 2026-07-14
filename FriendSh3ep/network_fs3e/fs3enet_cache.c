/*
 * FriendSh3ep network process - disk cache for downloaded media.
 * See fs3enet_cache.h.
 */

#include "fs3enet_cache.h"

#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

static BOOL  g_CacheReady = FALSE;
static char  g_CacheDir[512];  /* set once by FS3ECache_Init */
static ULONG g_MaxCacheBytes = 0;  /* 0 = unbounded; set once by FS3ECache_Init */

/* Hard cap on FS3ECache_EnforceLimit()'s delete-the-oldest iterations --
 * guards against looping forever if every remaining file happens to be
 * open/locked elsewhere and DeleteFile() keeps failing on the same
 * candidate. Generous for any realistic personal media cache; should
 * never actually be hit. */
#define FS3ECACHE_MAX_PRUNE_ATTEMPTS 10000

/* Defined further down (with the rest of the size-limit enforcement code,
 * near FS3ECache_Flush()); forward-declared so FS3ECache_Init() can call
 * it to trim a pre-existing oversized cache at startup. */
static void FS3ECache_EnforceLimit(void);

/* -------------------------------------------------------------------------
 * Recursive directory creation (AmigaOS mkdir -p equivalent)
 *
 * AmigaOS path rules that drive the split logic:
 *   "VOL:dir/sub"  → last '/'  splits "VOL:dir" / "sub"
 *   "VOL:leaf"     → no '/',   ':' splits volume root "VOL:" / "leaf"
 *   "VOL:"         → volume/assign root; Lock() tells us if it exists
 * ---------------------------------------------------------------------- */

static BOOL FS3ECache_MakeDir(const char *path)
{
    BPTR        lock;
    const char *sep;
    char        parent[512];
    ULONG       parentLen;

    lock = Lock(path, SHARED_LOCK);
    if (lock) { UnLock(lock); return TRUE; }

    sep = strrchr(path, '/');
    if (sep) {
        /* Parent is everything before the last '/'. */
        parentLen = (ULONG)(sep - path);
        if (parentLen == 0 || parentLen >= sizeof(parent)) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        if (!FS3ECache_MakeDir(parent)) return FALSE;
    } else {
        /* No slash: path is "VOL:leaf".  Parent is the volume/assign root
         * "VOL:" which must already exist (we can't create a volume). */
        sep = strchr(path, ':');
        if (!sep) return FALSE;  /* relative path with no drive — refuse */
        parentLen = (ULONG)(sep - path + 1);   /* include ':' */
        if (parentLen >= sizeof(parent)) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        lock = Lock(parent, SHARED_LOCK);
        if (!lock) return FALSE;   /* volume/assign offline */
        UnLock(lock);
    }

    lock = CreateDir(path);
    if (!lock) {
        /* Another process may have created it between our Lock check and
         * CreateDir — verify before reporting failure. */
        lock = Lock(path, SHARED_LOCK);
        if (!lock) return FALSE;
    }
    UnLock(lock);
    return TRUE;
}

/* -------------------------------------------------------------------------
 * FNV-1a 32-bit hash of url → 8-hex-digit filename.
 * Collision risk negligible for a personal media cache.
 * ---------------------------------------------------------------------- */

static ULONG FS3ECache_Hash(const char *url)
{
    ULONG hash = 2166136261UL;
    while (*url) {
        hash ^= (unsigned char)*url++;
        hash *= 16777619UL;
    }
    return hash;
}

void FS3ECache_ComputePath(const char *url, const char *subdir, char *out, ULONG outSize)
{
    if (subdir && subdir[0])
        snprintf(out, (size_t)outSize, "%s/%s/%08lx",
                 g_CacheDir, subdir, (unsigned long)FS3ECache_Hash(url));
    else
        snprintf(out, (size_t)outSize, "%s/%08lx",
                 g_CacheDir, (unsigned long)FS3ECache_Hash(url));
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

BOOL FS3ECache_Init(const char *cacheDir, ULONG maxSizeMB)
{
    ULONG len;

    if (!cacheDir || cacheDir[0] == '\0')
        cacheDir = FS3ECACHE_DEFAULT_DIR;

    snprintf(g_CacheDir, sizeof(g_CacheDir), "%s", cacheDir);

    /* Strip any trailing '/' to avoid double-slash in MakePath. */
    len = (ULONG)strlen(g_CacheDir);
    while (len > 0 && g_CacheDir[len - 1] == '/') g_CacheDir[--len] = '\0';

    if (!FS3ECache_MakeDir(g_CacheDir)) {
        g_CacheReady = FALSE;
        return FALSE;
    }

    /* maxSizeMB * 1024 * 1024 overflows ULONG at exactly 4096 (2^32
     * bytes) -- the largest value FS3ESettings validates -- so clamp
     * there instead of wrapping to 0, which would mean "prune
     * everything, always". */
    if (maxSizeMB == 0)         g_MaxCacheBytes = 0;
    else if (maxSizeMB >= 4096) g_MaxCacheBytes = 0xFFFFFFFFUL;
    else                        g_MaxCacheBytes = maxSizeMB * 1024UL * 1024UL;

    g_CacheReady = TRUE;

    /* Trim a pre-existing oversized cache (grown unbounded under an
     * older build, or now over a lowered limit) right away, not just
     * capped going forward. */
    FS3ECache_EnforceLimit();

    return TRUE;
}

void FS3ECache_Cleanup(void)
{
    g_CacheReady = FALSE;
}

/* Delete every plain file directly under dirPath (the directory itself,
 * and any subdirectory entries within it, are left alone). Shared by
 * FS3ECache_Flush() for both the cache root and each of its immediate
 * purpose subdirectories (usericons/, thumbnails/, ...). */
static BOOL FS3ECache_FlushDir(const char *dirPath)
{
    BPTR                   lock;
    struct FileInfoBlock  *fib;
    char                   path[FS3ECACHE_PATH_SIZE];
    BOOL                   ok = TRUE;

    lock = Lock(dirPath, SHARED_LOCK);
    if (!lock) return FALSE;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) {
        UnLock(lock);
        return FALSE;
    }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType < 0) {   /* plain file, not a dir */
                snprintf(path, sizeof(path), "%s/%s", dirPath, fib->fib_FileName);
                if (!DeleteFile(path)) ok = FALSE;
            }
        }
    } else {
        ok = FALSE;
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return ok;
}

BOOL FS3ECache_Flush(void)
{
    BPTR                   lock;
    struct FileInfoBlock  *fib;
    char                   subPath[FS3ECACHE_PATH_SIZE];
    BOOL                   ok;

    if (!g_CacheReady) return FALSE;

    /* Files directly under the cache root (anything not routed to a
     * purpose subdir, e.g. custom emoji). */
    ok = FS3ECache_FlushDir(g_CacheDir);

    /* One level of subdirectories (usericons/, thumbnails/, ...) --
     * emptied in place, not removed, so Store() doesn't need to recreate
     * them on the next fetch. */
    lock = Lock(g_CacheDir, SHARED_LOCK);
    if (!lock) return ok;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) {
        UnLock(lock);
        return ok;
    }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType >= 0) {   /* directory */
                snprintf(subPath, sizeof(subPath), "%s/%s", g_CacheDir, fib->fib_FileName);
                if (!FS3ECache_FlushDir(subPath)) ok = FALSE;
            }
        }
    } else {
        ok = FALSE;
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return ok;
}

/* -------------------------------------------------------------------------
 * Size-limit enforcement: real directory scans, not a maintained running
 * total. A tracked byte counter would miss the resized-thumbnail sibling
 * *.WxH.bmp files bmimage.c's BmImage_GenerateScaledBmp() writes directly
 * next to a cached original -- those never go through FS3ECache_Store(),
 * so anything counting only Store() writes would silently undercount real
 * disk usage. A live scan sees the true state of the directory tree
 * regardless of who wrote what. Called rarely enough (once per Store(),
 * i.e. once per cache miss, not once per cache *hit*) that the O(n) cost
 * doesn't matter here the way it would on a hot path.
 * ---------------------------------------------------------------------- */

/* Adds the size of every plain file directly under dirPath to *total.
 * Mirrors FS3ECache_FlushDir's one-level walk but sums instead of
 * deletes. Saturates at ULONG max rather than wrapping on overflow -- a
 * cache that's already many times over budget (e.g. one that grew
 * unbounded under a build before this limit existed) still reads as
 * "way over", no need for byte-exact precision once it's clearly over. */
static void FS3ECache_SizeDir(const char *dirPath, ULONG *total)
{
    BPTR                   lock;
    struct FileInfoBlock  *fib;

    lock = Lock(dirPath, SHARED_LOCK);
    if (!lock) return;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); return; }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType < 0) {   /* plain file, not a dir */
                ULONG size = (ULONG)fib->fib_Size;
                *total = (*total > (ULONG)~0UL - size) ? (ULONG)~0UL : *total + size;
            }
        }
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
}

/* Total bytes across the cache root and every immediate subdirectory --
 * same one-level walk shape as FS3ECache_Flush(). */
static ULONG FS3ECache_TotalSize(void)
{
    ULONG                  total = 0;
    BPTR                   lock;
    struct FileInfoBlock  *fib;
    char                   subPath[FS3ECACHE_PATH_SIZE];

    FS3ECache_SizeDir(g_CacheDir, &total);

    lock = Lock(g_CacheDir, SHARED_LOCK);
    if (!lock) return total;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); return total; }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType >= 0) {   /* directory */
                snprintf(subPath, sizeof(subPath), "%s/%s", g_CacheDir, fib->fib_FileName);
                FS3ECache_SizeDir(subPath, &total);
            }
        }
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return total;
}

/* Scans dirPath (one level, plain files only) for the file with the
 * oldest fib_Date, updating *bestDate, outPath, and *outFound if it's
 * older than whatever's already been found -- called once per directory by
 * FS3ECache_DeleteOldest() below so the comparison spans the whole cache
 * tree, not just one directory. Day+minute granularity (ds_Days/
 * ds_Minute) is plenty for LRU-style eviction; ds_Tick isn't needed to
 * break ties meaningfully here. */
static void FS3ECache_FindOldestInDir(const char *dirPath, struct DateStamp *bestDate,
                                       char *outPath, ULONG outPathSize, BOOL *outFound)
{
    BPTR                   lock;
    struct FileInfoBlock  *fib;

    lock = Lock(dirPath, SHARED_LOCK);
    if (!lock) return;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); return; }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType < 0) {   /* plain file, not a dir */
                BOOL older = !*outFound ||
                             fib->fib_Date.ds_Days  <  bestDate->ds_Days ||
                             (fib->fib_Date.ds_Days == bestDate->ds_Days &&
                              fib->fib_Date.ds_Minute < bestDate->ds_Minute);
                if (older) {
                    *bestDate = fib->fib_Date;
                    snprintf(outPath, (size_t)outPathSize, "%s/%s", dirPath, fib->fib_FileName);
                    *outFound = TRUE;
                }
            }
        }
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
}

/* Deletes the single oldest plain file anywhere in the cache tree (root
 * or one level of subdirectories). Returns FALSE if the cache is empty,
 * or if the chosen file couldn't be deleted (e.g. still open elsewhere --
 * FS3ECache_EnforceLimit's attempt cap keeps that from looping forever). */
static BOOL FS3ECache_DeleteOldest(void)
{
    struct DateStamp        bestDate;
    char                     path[FS3ECACHE_PATH_SIZE];
    BOOL                     found = FALSE;
    BPTR                     lock;
    struct FileInfoBlock    *fib;
    char                     subPath[FS3ECACHE_PATH_SIZE];

    memset(&bestDate, 0, sizeof(bestDate));

    FS3ECache_FindOldestInDir(g_CacheDir, &bestDate, path, sizeof(path), &found);

    lock = Lock(g_CacheDir, SHARED_LOCK);
    if (lock) {
        fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (fib) {
            if (Examine(lock, fib)) {
                while (ExNext(lock, fib)) {
                    if (fib->fib_DirEntryType >= 0) {   /* directory */
                        snprintf(subPath, sizeof(subPath), "%s/%s", g_CacheDir, fib->fib_FileName);
                        FS3ECache_FindOldestInDir(subPath, &bestDate, path, sizeof(path), &found);
                    }
                }
            }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(lock);
    }

    if (!found) return FALSE;
    return DeleteFile(path) ? TRUE : FALSE;
}

/* Deletes the oldest cached files until the total is back under
 * g_MaxCacheBytes, or gives up after FS3ECACHE_MAX_PRUNE_ATTEMPTS. No-op
 * if g_MaxCacheBytes is 0 (unbounded) or Init() hasn't succeeded. */
static void FS3ECache_EnforceLimit(void)
{
    ULONG attempts;

    if (!g_CacheReady || g_MaxCacheBytes == 0) return;

    for (attempts = 0; attempts < FS3ECACHE_MAX_PRUNE_ATTEMPTS; attempts++) {
        if (FS3ECache_TotalSize() <= g_MaxCacheBytes) return;
        if (!FS3ECache_DeleteOldest()) return;
    }
}

BOOL FS3ECache_Lookup(const char *url, const char *subdir, char *outPath, ULONG pathSize)
{
    BPTR fh;

    if (!g_CacheReady || !url) return FALSE;

    FS3ECache_ComputePath(url, subdir, outPath, pathSize);

    fh = Open(outPath, MODE_OLDFILE);
    if (!fh) return FALSE;
    Close(fh);
    return TRUE;
}

BOOL FS3ECache_EnsureSubdir(const char *subdir)
{
    char subPath[FS3ECACHE_PATH_SIZE];

    if (!g_CacheReady) return FALSE;
    if (!subdir || !subdir[0]) return TRUE;   /* cache root itself already exists (Init) */

    snprintf(subPath, sizeof(subPath), "%s/%s", g_CacheDir, subdir);
    return FS3ECache_MakeDir(subPath);
}

BOOL FS3ECache_Store(const char *url, const char *subdir, const void *data, ULONG dataLen,
                     char *outPath, ULONG pathSize)
{
    BPTR fh;
    LONG written;

    if (!g_CacheReady || !url || !data || dataLen == 0) return FALSE;

    if (!FS3ECache_EnsureSubdir(subdir)) return FALSE;

    FS3ECache_ComputePath(url, subdir, outPath, pathSize);

    fh = Open(outPath, MODE_NEWFILE);
    if (!fh) return FALSE;

    written = Write(fh, (APTR)data, (LONG)dataLen);
    Close(fh);

    if (written != (LONG)dataLen) {
        DeleteFile(outPath);   /* don't leave a truncated file in the cache */
        return FALSE;
    }

    /* Keep the persistent cache under its configured size budget. Always
     * picks the oldest file(s) to evict, so the one just written here is
     * only ever at risk itself in the pathological case of a single
     * download bigger than the whole configured limit with nothing older
     * left to evict instead -- not worth guarding further for a personal
     * avatar/thumbnail cache. */
    FS3ECache_EnforceLimit();

    return TRUE;
}

static void FS3ECache_ComputeRAMPath(const char *url, char *outPath, ULONG pathSize)
{
    snprintf(outPath, (size_t)pathSize, "%s/%08lx",
             FS3ECACHE_RAM_TEMP_DIR, (unsigned long)FS3ECache_Hash(url));
}

BOOL FS3ECache_LookupRAM(const char *url, char *outPath, ULONG pathSize)
{
    BPTR fh;

    if (!url) return FALSE;

    FS3ECache_ComputeRAMPath(url, outPath, pathSize);

    fh = Open(outPath, MODE_OLDFILE);
    if (!fh) return FALSE;
    Close(fh);
    return TRUE;
}

BOOL FS3ECache_StoreRAM(const char *url, const void *data, ULONG dataLen,
                        char *outPath, ULONG pathSize)
{
    BPTR fh;
    LONG written;

    if (!url || !data || dataLen == 0) return FALSE;

    /* Independent of g_CacheReady/g_CacheDir -- RAM:T should work even if
     * the persistent cache dir failed to init. */
    if (!FS3ECache_MakeDir(FS3ECACHE_RAM_TEMP_DIR)) return FALSE;

    FS3ECache_ComputeRAMPath(url, outPath, pathSize);

    fh = Open(outPath, MODE_NEWFILE);
    if (!fh) return FALSE;

    written = Write(fh, (APTR)data, (LONG)dataLen);
    Close(fh);

    if (written != (LONG)dataLen) {
        DeleteFile(outPath);
        return FALSE;
    }

    return TRUE;
}
