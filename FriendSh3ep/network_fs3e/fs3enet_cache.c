/*
 * FriendSh3ep network process - disk cache for downloaded media.
 * See fs3enet_cache.h.
 */

#include "fs3enet_cache.h"

#include <dos/dos.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <proto/exec.h>

#include <stdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

static BOOL  g_CacheReady = FALSE;
static char *g_CacheDir = NULL;    /* AllocVec'd; set once by FS3ECache_Init */
static ULONG g_MaxCacheBytes = 0;  /* 0 = unbounded; set once by FS3ECache_Init */

/* -------------------------------------------------------------------------
 * Small AllocVec'd string helpers
 *
 * Paths here can no longer be assumed to fit some fixed stack buffer: the
 * cache directory itself is user-configurable, AmigaOS dir nesting can go
 * arbitrarily deep, and a task's stack is a scarcer resource on real
 * hardware than heap. Every path is therefore built to its exact needed
 * size on the heap instead.
 * ---------------------------------------------------------------------- */

/* AllocVec'd strdup. NULL in yields NULL out. */
static char *FS3ECache_StrDup(const char *s)
{
    ULONG len;
    char *out;

    if (!s) return NULL;

    len = (ULONG)strlen(s) + 1;
    out = (char *)AllocVec(len, MEMF_ANY);
    if (out) memcpy(out, s, len);
    return out;
}

/* Returns an AllocVec'd "a/b", or NULL on allocation failure or a missing
 * argument. Caller must FreeVec() the result. */
static char *FS3ECache_JoinPath(const char *a, const char *b)
{
    ULONG lenA, lenB;
    char *out;

    if (!a || !b) return NULL;

    lenA = (ULONG)strlen(a);
    lenB = (ULONG)strlen(b);
    out = (char *)AllocVec(lenA + 1 + lenB + 1, MEMF_ANY);
    if (!out) return NULL;

    memcpy(out, a, lenA);
    out[lenA] = '/';
    memcpy(out + lenA + 1, b, lenB);
    out[lenA + 1 + lenB] = '\0';
    return out;
}

/* Hard cap on FS3ECache_EnforceLimit()'s delete-the-oldest iterations --
 * guards against looping forever if every remaining file happens to be
 * open/locked elsewhere and DeleteFile() keeps failing on the same
 * candidate. Generous for any realistic personal media cache; should
 * never actually be hit. */
#define FS3ECACHE_MAX_PRUNE_ATTEMPTS 10000

/* Defined further down (with the rest of the size-limit enforcement code,
 * near FS3ECache_Flush()); forward-declared so FS3ECache_Init() can call
 * it to trim a pre-existing oversized cache at startup. Public (see
 * fs3enet_cache.h) so a caller writing to the persistent cache
 * incrementally, outside FS3ECache_Store(), can trigger it once done. */
void FS3ECache_EnforceLimit(void);

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
    char       *parent;
    ULONG       parentLen;
    BOOL        parentOk;

    lock = Lock(path, SHARED_LOCK);
    if (lock) { UnLock(lock); return TRUE; }

    sep = strrchr(path, '/');
    if (sep) {
        /* Parent is everything before the last '/'. */
        parentLen = (ULONG)(sep - path);
        if (parentLen == 0) return FALSE;
        parent = (char *)AllocVec(parentLen + 1, MEMF_ANY);
        if (!parent) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        parentOk = FS3ECache_MakeDir(parent);
        FreeVec(parent);
        if (!parentOk) return FALSE;
    } else {
        /* No slash: path is "VOL:leaf".  Parent is the volume/assign root
         * "VOL:" which must already exist (we can't create a volume). */
        sep = strchr(path, ':');
        if (!sep) return FALSE;  /* relative path with no drive — refuse */
        parentLen = (ULONG)(sep - path + 1);   /* include ':' */
        parent = (char *)AllocVec(parentLen + 1, MEMF_ANY);
        if (!parent) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        lock = Lock(parent, SHARED_LOCK);
        FreeVec(parent);
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

char *FS3ECache_ComputePath(const char *url, const char *subdir)
{
    char  hash[9];  /* 8 hex digits + NUL -- fixed width, not path-derived */
    char *out;

    snprintf(hash, sizeof(hash), "%08lx", (unsigned long)FS3ECache_Hash(url));

    if (subdir && subdir[0]) {
        char *dir = FS3ECache_JoinPath(g_CacheDir, subdir);
        if (!dir) return NULL;
        out = FS3ECache_JoinPath(dir, hash);
        FreeVec(dir);
    } else {
        out = FS3ECache_JoinPath(g_CacheDir, hash);
    }

    return out;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

BOOL FS3ECache_Init(const char *cacheDir, ULONG maxSizeMB)
{
    ULONG len;

    if (!cacheDir || cacheDir[0] == '\0')
        cacheDir = FS3ECACHE_DEFAULT_DIR;

    /* Called exactly once per network-process lifetime in practice (see
     * fs3enet.c's FS3ENet_ProcEntry) -- the FreeVec() here is defensive,
     * not something a live re-Init() actually needs to support. */
    FreeVec(g_CacheDir);
    g_CacheDir = FS3ECache_StrDup(cacheDir);
    if (!g_CacheDir) {
        g_CacheReady = FALSE;
        return FALSE;
    }

    /* Strip any trailing '/' to avoid double-slash when joining. */
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
    FreeVec(g_CacheDir);
    g_CacheDir = NULL;
}

/* Delete every plain file directly under dirPath (the directory itself,
 * and any subdirectory entries within it, are left alone). Shared by
 * FS3ECache_Flush() for both the cache root and each of its immediate
 * purpose subdirectories (usericons/, thumbnails/, ...). */
static BOOL FS3ECache_FlushDir(const char *dirPath)
{
    BPTR                   lock;
    struct FileInfoBlock  *fib;
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
                char *path = FS3ECache_JoinPath(dirPath, (const char *)fib->fib_FileName);
                if (!path || !DeleteFile(path)) ok = FALSE;
                FreeVec(path);
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
                char *subPath = FS3ECache_JoinPath(g_CacheDir, (const char *)fib->fib_FileName);
                if (!subPath || !FS3ECache_FlushDir(subPath)) ok = FALSE;
                FreeVec(subPath);
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

    FS3ECache_SizeDir(g_CacheDir, &total);

    lock = Lock(g_CacheDir, SHARED_LOCK);
    if (!lock) return total;

    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (!fib) { UnLock(lock); return total; }

    if (Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            if (fib->fib_DirEntryType >= 0) {   /* directory */
                char *subPath = FS3ECache_JoinPath(g_CacheDir, (const char *)fib->fib_FileName);
                if (subPath) FS3ECache_SizeDir(subPath, &total);
                FreeVec(subPath);
            }
        }
    }

    FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    return total;
}

/* Scans dirPath (one level, plain files only) for the file with the
 * oldest fib_Date, updating *bestDate, *bestPath (AllocVec'd, replacing
 * whatever it previously pointed to), and *found if it's older than
 * whatever's already been found -- called once per directory by
 * FS3ECache_DeleteOldest() below so the comparison spans the whole cache
 * tree, not just one directory. Day+minute granularity (ds_Days/
 * ds_Minute) is plenty for LRU-style eviction; ds_Tick isn't needed to
 * break ties meaningfully here. */
static void FS3ECache_FindOldestInDir(const char *dirPath, struct DateStamp *bestDate,
                                       char **bestPath, BOOL *found)
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
                BOOL older = !*found ||
                             fib->fib_Date.ds_Days  <  bestDate->ds_Days ||
                             (fib->fib_Date.ds_Days == bestDate->ds_Days &&
                              fib->fib_Date.ds_Minute < bestDate->ds_Minute);
                if (older) {
                    char *path = FS3ECache_JoinPath(dirPath, (const char *)fib->fib_FileName);
                    if (path) {
                        FreeVec(*bestPath);
                        *bestPath = path;
                        *bestDate = fib->fib_Date;
                        *found = TRUE;
                    }
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
    char                    *path = NULL;
    BOOL                     found = FALSE;
    BPTR                     lock;
    struct FileInfoBlock    *fib;

    memset(&bestDate, 0, sizeof(bestDate));

    FS3ECache_FindOldestInDir(g_CacheDir, &bestDate, &path, &found);

    lock = Lock(g_CacheDir, SHARED_LOCK);
    if (lock) {
        fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (fib) {
            if (Examine(lock, fib)) {
                while (ExNext(lock, fib)) {
                    if (fib->fib_DirEntryType >= 0) {   /* directory */
                        char *subPath = FS3ECache_JoinPath(g_CacheDir, (const char *)fib->fib_FileName);
                        if (subPath) FS3ECache_FindOldestInDir(subPath, &bestDate, &path, &found);
                        FreeVec(subPath);
                    }
                }
            }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(lock);
    }

    if (!found) { FreeVec(path); return FALSE; }
    {
        BOOL ok = DeleteFile(path) ? TRUE : FALSE;
        FreeVec(path);
        return ok;
    }
}

/* Deletes the oldest cached files until the total is back under
 * g_MaxCacheBytes, or gives up after FS3ECACHE_MAX_PRUNE_ATTEMPTS. No-op
 * if g_MaxCacheBytes is 0 (unbounded) or Init() hasn't succeeded. */
void FS3ECache_EnforceLimit(void)
{
    ULONG attempts;

    if (!g_CacheReady || g_MaxCacheBytes == 0) return;

    for (attempts = 0; attempts < FS3ECACHE_MAX_PRUNE_ATTEMPTS; attempts++) {
        if (FS3ECache_TotalSize() <= g_MaxCacheBytes) return;
        if (!FS3ECache_DeleteOldest()) return;
    }
}

char *FS3ECache_Lookup(const char *url, const char *subdir, BOOL *found)
{
    char *path;
    BPTR  fh;

    *found = FALSE;
    if (!url) return NULL;

    path = FS3ECache_ComputePath(url, subdir);
    if (!path) return NULL;

    if (g_CacheReady) {
        fh = Open(path, MODE_OLDFILE);
        if (fh) { Close(fh); *found = TRUE; }
    }

    return path;
}

BOOL FS3ECache_EnsureSubdir(const char *subdir)
{
    char *subPath;
    BOOL  ok;

    if (!g_CacheReady) return FALSE;
    if (!subdir || !subdir[0]) return TRUE;   /* cache root itself already exists (Init) */

    subPath = FS3ECache_JoinPath(g_CacheDir, subdir);
    if (!subPath) return FALSE;
    ok = FS3ECache_MakeDir(subPath);
    FreeVec(subPath);
    return ok;
}

char *FS3ECache_Store(const char *url, const char *subdir, const void *data, ULONG dataLen)
{
    char *path;
    BPTR  fh;
    LONG  written;

    if (!g_CacheReady || !url || !data || dataLen == 0) return NULL;

    if (!FS3ECache_EnsureSubdir(subdir)) return NULL;

    path = FS3ECache_ComputePath(url, subdir);
    if (!path) return NULL;

    fh = Open(path, MODE_NEWFILE);
    if (!fh) { FreeVec(path); return NULL; }

    written = Write(fh, (APTR)data, (LONG)dataLen);
    Close(fh);

    if (written != (LONG)dataLen) {
        DeleteFile(path);   /* don't leave a truncated file in the cache */
        FreeVec(path);
        return NULL;
    }

    /* Keep the persistent cache under its configured size budget. Always
     * picks the oldest file(s) to evict, so the one just written here is
     * only ever at risk itself in the pathological case of a single
     * download bigger than the whole configured limit with nothing older
     * left to evict instead -- not worth guarding further for a personal
     * avatar/thumbnail cache. */
    FS3ECache_EnforceLimit();

    return path;
}

static char *FS3ECache_ComputeRAMPath(const char *url)
{
    char hash[9];  /* 8 hex digits + NUL -- fixed width, not path-derived */

    snprintf(hash, sizeof(hash), "%08lx", (unsigned long)FS3ECache_Hash(url));
    return FS3ECache_JoinPath(FS3ECACHE_RAM_TEMP_DIR, hash);
}

BOOL FS3ECache_EnsureRAMTempDir(void)
{
    return FS3ECache_MakeDir(FS3ECACHE_RAM_TEMP_DIR);
}

char *FS3ECache_LookupRAM(const char *url, BOOL *found)
{
    char *path;
    BPTR  fh;

    *found = FALSE;
    if (!url) return NULL;

    path = FS3ECache_ComputeRAMPath(url);
    if (!path) return NULL;

    fh = Open(path, MODE_OLDFILE);
    if (fh) { Close(fh); *found = TRUE; }

    return path;
}

char *FS3ECache_StoreRAM(const char *url, const void *data, ULONG dataLen)
{
    char *path;
    BPTR  fh;
    LONG  written;

    if (!url || !data || dataLen == 0) return NULL;

    /* Independent of g_CacheReady/g_CacheDir -- RAM:T should work even if
     * the persistent cache dir failed to init. */
    if (!FS3ECache_MakeDir(FS3ECACHE_RAM_TEMP_DIR)) return NULL;

    path = FS3ECache_ComputeRAMPath(url);
    if (!path) return NULL;

    fh = Open(path, MODE_NEWFILE);
    if (!fh) { FreeVec(path); return NULL; }

    written = Write(fh, (APTR)data, (LONG)dataLen);
    Close(fh);

    if (written != (LONG)dataLen) {
        DeleteFile(path);
        FreeVec(path);
        return NULL;
    }

    return path;
}
