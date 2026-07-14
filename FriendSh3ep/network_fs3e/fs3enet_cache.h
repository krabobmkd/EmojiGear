#ifndef FS3ENET_CACHE_H
#define FS3ENET_CACHE_H

/*
 * FriendSh3ep network process - disk cache for downloaded media.
 *
 * Avatar images, status attachment thumbnails, and custom emoji are stored
 * in a configurable directory using the FNV-1a 32-bit hash of the source
 * URL as the filename (no extension: Amiga datatypes detect JPEG/PNG from
 * magic bytes, so the extension is irrelevant and omitting it simplifies
 * cache lookup to a single Open() probe).
 *
 * Lookup/Store take an optional `subdir` (e.g. "usericons", "thumbnails")
 * so different purposes land in their own subdirectory of the cache root
 * instead of one flat pile of hash-named files -- pass NULL/"" for the old
 * flat-root behaviour (still used for anything that doesn't care, e.g.
 * custom emoji). The subdirectory is created lazily on first Store(), the
 * same recursive mkdir-p this file already does for the root.
 *
 * The cache directory is set once at Init() time and persists for the
 * lifetime of the network process.  It is created recursively if absent
 * (fs3enet_cache.c contains an AmigaOS-aware mkdir-p equivalent).
 *
 * All functions must be called from the network process task only.
 */

#include <exec/types.h>

/* Default cache directory used when the caller passes NULL or "".
 * PROGDIR: resolves to the directory containing the FriendSh3ep binary.
 * The dot prefix follows the Amiga convention for hidden/app-private dirs. */
#define FS3ECACHE_DEFAULT_DIR "PROGDIR:.cache"

/* Where FS3ECache_StoreRAM() puts downloads the caller doesn't want kept
 * on disk (see "Keep big user icons/thumbnails" in Settings) -- RAM:T
 * specifically, not T: (T: isn't guaranteed to be RAM-backed; some setups
 * point it at a real, slower drive, defeating the whole point). */
#define FS3ECACHE_RAM_TEMP_DIR "RAM:T"

/* Maximum length of a cache file path including NUL. */
#define FS3ECACHE_PATH_SIZE 256

/*
 * Set the cache directory and create it (recursively) if absent.
 * Pass NULL or "" to use FS3ECACHE_DEFAULT_DIR.
 * Returns FALSE only if the directory cannot be created; in that case
 * FS3ECache_Lookup/Store always return FALSE, but the rest of the network
 * process continues to work.
 *
 * maxSizeMB bounds the cache directory's total on-disk size (0 = unbounded).
 * FS3ECache_Store() enforces this after every write by deleting the oldest
 * cached files (by fib_Date, across the cache root and every immediate
 * subdirectory -- usericons/, thumbnails/, ..., which also naturally
 * accounts for the resized-thumbnail sibling *.WxH.bmp files
 * BmImage_GenerateScaledBmp writes directly, not through this API) until
 * back under the limit. Init() itself also enforces it once at startup, so
 * a cache that grew past the limit under an older build (or after the user
 * lowers the setting) gets trimmed immediately rather than only capped
 * going forward.
 */
BOOL FS3ECache_Init(const char *cacheDir, ULONG maxSizeMB);

/* Reserved for future use (eviction, index flush, etc.). No-op for now. */
void FS3ECache_Cleanup(void);

/*
 * Delete every file directly under the cache directory (the directory
 * itself is kept). Called from FS3ENETQ_FLUSH_CACHE (see fs3enet.h) in
 * response to the SettingsView "Flush cache" button.
 * Returns FALSE if Init() failed or the directory couldn't be examined;
 * a single file that fails to delete does not abort the rest.
 */
BOOL FS3ECache_Flush(void);

/*
 * If url is already cached under subdir (NULL/"" = cache root), fills
 * outPath with the local AmigaOS path and returns TRUE.  Returns FALSE
 * when the file is absent or Init() failed.
 */
BOOL FS3ECache_Lookup(const char *url, const char *subdir, char *outPath, ULONG pathSize);

/*
 * Creates cache/<subdir> (recursively, idempotent) if it doesn't already
 * exist. NULL/"" is a no-op returning TRUE (the cache root itself already
 * exists after Init()). FS3ECache_Store() calls this itself; exposed
 * separately for callers that write to a cache/<subdir>/... path without
 * going through Store() -- e.g. a resized thumbnail written directly by
 * the thumbnail process from a cacheKeyPath rooted there, even when the
 * *original* it was generated from went to FS3ECACHE_RAM_TEMP_DIR
 * instead (see fs3ethumb.h's fs3etm_CacheKeyPath and "Keep big user
 * icons/thumbnails" in Settings) and so never created this subdir itself.
 */
BOOL FS3ECache_EnsureSubdir(const char *subdir);

/*
 * Writes data to the cache as a new file keyed by url, under subdir
 * (NULL/"" = cache root; created if absent).  Fills outPath.
 * Returns FALSE on I/O error or if Init() failed.
 * A truncated write deletes the partial file to keep the cache clean.
 */
BOOL FS3ECache_Store(const char *url, const char *subdir, const void *data, ULONG dataLen,
                     char *outPath, ULONG pathSize);

/*
 * Pure path computation, no I/O, no Init() dependency: fills outPath with
 * the deterministic path url would live at under subdir (same hash-based
 * naming Lookup/Store use) without checking whether anything is actually
 * there. Lets a caller derive a stable name for something related (e.g. a
 * resized sibling thumbnail, see BmImage_GenerateScaledBmp's cacheKeyPath)
 * even when the original itself isn't going to be stored persistently --
 * see FS3ECache_StoreRAM.
 */
void FS3ECache_ComputePath(const char *url, const char *subdir, char *outPath, ULONG pathSize);

/*
 * Like FS3ECache_Store, but always writes to FS3ECACHE_RAM_TEMP_DIR
 * (verified/created here, independent of the persistent cache dir/Init())
 * instead of under the persistent cache -- for downloads the caller plans
 * to delete right after use (see fs3ethumb.h's fs3etm_DeleteSrcAfter).
 */
BOOL FS3ECache_StoreRAM(const char *url, const void *data, ULONG dataLen,
                        char *outPath, ULONG pathSize);

/*
 * Checks whether url's RAM:T temp file (the exact deterministic path
 * FS3ECache_StoreRAM() would write to) is already present, without
 * writing anything. Callers should check this *before* re-downloading and
 * calling FS3ECache_StoreRAM() for a URL they don't intend to persist:
 * StoreRAM's Open(...,MODE_NEWFILE) silently truncates/replaces whatever
 * is already at that path, which is exactly wrong if another consumer
 * (e.g. the thumbnail process, mid-decode of an earlier still-in-flight
 * request for the same URL) still has it open for reading -- a
 * write-while-read race that real trackdisk devices mostly tolerate but
 * UAE's shared-filesystem driver has been observed not to. Fills outPath
 * either way (same as FS3ECache_Lookup); returns TRUE only if the file
 * exists.
 */
BOOL FS3ECache_LookupRAM(const char *url, char *outPath, ULONG pathSize);

#endif /* FS3ENET_CACHE_H */
