#ifndef FS3ETHUMB_H
#define FS3ETHUMB_H

/*
 * fs3ethumb.h - FriendSh3ep thumbnail process.
 *
 * Decoding a full-size avatar/attachment image with picture.datatype and
 * box-fit scaling it (see bmimage.c's BmImage_GenerateScaledBmp) is
 * CPU-bound and can take noticeable time on a 68020/030 -- doing it on the
 * GUI task would freeze the window for the duration. This process runs
 * that work on its own AmigaDOS task instead, the same
 * CreateNewProcTags()/MsgPort request-reply shape as
 * network_fs3e/fs3enet.c uses to move HTTP I/O off the GUI task -- just
 * CPU work instead of network I/O. See FriendSh3ep/PlanToReworkThumbnails.txt.
 *
 * Unlike fs3enet's process, this one needs no library opens of its own: it
 * is entered via CreateNewProcTags() from within the running GUI
 * executable, so it shares that executable's global/static data --
 * including the already-open DataTypesBase (see bmimage.c) -- the same way
 * fs3enet.c's g_FS3ENetStartup global is visible to its child task. No
 * OpenLibrary("datatypes.library", ...) needed here.
 *
 * Request/reply flow:
 *   1. GUI calls FS3EThumb_Request() with a source file path already on
 *      disk (typically network_fs3e's FETCH_IMAGE reply path) and a target
 *      box size (64x64 for avatars).
 *   2. This process calls BmImage_GenerateScaledBmp() to produce (or reuse)
 *      the "<srcPath>.<W>x<H>.bmp" thumbnail file.
 *   3. It replies with that path; the GUI task loads the finished BMP into
 *      a screen bitmap on its own time (cheap: no decode/scale work
 *      remains, the file is already the target size or smaller).
 */

#include <exec/ports.h>
#include <exec/types.h>

enum FS3EThumbRequestType
{
    FS3ETHUMBQ_SHUTDOWN = 0,  /* ask the thumbnail process to exit */
    FS3ETHUMBQ_MAKE           /* decode + box-fit-scale one image to a BMP thumbnail */
};

enum FS3EThumbResult
{
    FS3ETHUMBR_OK = 0,
    FS3ETHUMBR_ERROR
};

/* What fs3etm_Key identifies and which live-cache pool the reply belongs
 * in -- set by the caller, simply echoed back untouched (this message is
 * one AllocVec'd block reused for both directions, not packed req/reply
 * blocks, so anything the caller fills in is still there on reply; see
 * FS3EThumb_Request). */
enum FS3EThumbKind
{
    FS3ETHUMB_KIND_AVATAR = 0,  /* fs3etm_Key is an @acct */
    FS3ETHUMB_KIND_MEDIA  = 1,  /* fs3etm_Key is the attachment's preview/URL */
    FS3ETHUMB_KIND_CARD   = 2   /* fs3etm_Key is a link preview card's image URL --
                                 * this process itself never branches on fs3etm_Kind
                                 * (width/height are separate explicit args below), it's
                                 * purely an echo the GUI uses to route the reply to the
                                 * right AvatarImages_*ThumbReady pool (see
                                 * FS3EApp_HandleThumbReply) */
};

#define FS3ETHUMB_PATH_SIZE 256
/* Long enough for a full media CDN URL (often 150-250+ chars with signed
 * query params), not just the short @acct strings avatar requests use. */
#define FS3ETHUMB_KEY_SIZE  384

/* Fixed avatar thumbnail box size (see PlanToReworkThumbnails.txt): user
 * icons are minified once to this size regardless of the current
 * font/DPI-driven avatarSize, so the disk cache holds one thumbnail per
 * user instead of one per DPI ever used. AvatarImages_ThumbReady() then
 * does its own (cheap, since the source is already this small) box-fit
 * scale down to the live avatarSize. */
#define FS3ETHUMB_AVATAR_SIZE 64

/* Media preview thumbnail target: scale to this width, height following
 * the source aspect ratio, up to FS3ETHUMB_MEDIA_HEIGHT_CAP -- passed to
 * BmImage_GenerateScaledBmp() as a target *box* (targetW x targetH), which
 * fits the image inside that box touching whichever axis is tighter (see
 * bmimage.h). Landscape/square attachments (the common case) end up
 * exactly FS3ETHUMB_MEDIA_WIDTH wide; only an unusually tall portrait
 * image would come out narrower than that, height-capped instead. */
#define FS3ETHUMB_MEDIA_WIDTH      200
#define FS3ETHUMB_MEDIA_HEIGHT_CAP 600

/*
 * Fixed-layout request/reply message (no separate AllocVec'd data block,
 * unlike FS3ENetMessage -- the two paths and two dimensions here always
 * fit fixed-size fields, so there is no variable-length payload to pack).
 * Allocated with AllocVec() by FS3EThumb_Request() and freed by the
 * receiver once drained, exactly like FS3ENetMessage's lifecycle.
 */
typedef struct FS3EThumbMessage
{
    struct Message fs3etm_Msg;
    ULONG          fs3etm_Type;    /* enum FS3EThumbRequestType */
    ULONG          fs3etm_Result;  /* enum FS3EThumbResult, set on reply */

    /* FS3ETHUMBQ_MAKE request fields, filled by the caller. */
    char  fs3etm_SrcPath[FS3ETHUMB_PATH_SIZE];
    char  fs3etm_Key[FS3ETHUMB_KEY_SIZE];   /* echoed back; e.g. @acct or a media URL */
    ULONG fs3etm_Kind;                      /* enum FS3EThumbKind; echoed back */
    UWORD fs3etm_TargetW;
    UWORD fs3etm_TargetH;

    /* Deterministic path (no extension) the resulting thumbnail should be
     * named/found after, independent of where fs3etm_SrcPath actually
     * lives -- passed straight through to BmImage_GenerateScaledBmp's
     * cacheKeyPath (see bmimage.h). Empty = derive the name from SrcPath
     * itself (the common case: source and thumbnail are siblings). Needed
     * when SrcPath is a transient RAM:T download (see fs3etm_DeleteSrcAfter)
     * but the thumbnail itself must still get a name stable across runs --
     * see FS3ENetFetchImageReply.fs3enf_CachePath, which is exactly this
     * path for the source URL. */
    char  fs3etm_CacheKeyPath[FS3ETHUMB_PATH_SIZE];
    /* TRUE = delete fs3etm_SrcPath once this request is handled (success
     * or failure) -- set when the caller downloaded it to RAM:T rather
     * than the persistent cache (see FS3ENetFetchImageReply.fs3enf_IsTemp
     * and the "Keep big user icons/thumbnails" settings). */
    BOOL  fs3etm_DeleteSrcAfter;

    /* FS3ETHUMBQ_MAKE reply field, filled by the thumbnail process on
     * success. Deterministic ("<CacheKeyPath or SrcPath>.<TargetW>x
     * <TargetH>.bmp") so the GUI could derive it itself, but it's handed
     * back anyway to keep the reply self-contained. Empty on
     * FS3ETHUMBR_ERROR. */
    char  fs3etm_ThumbPath[FS3ETHUMB_PATH_SIZE];

    /* FS3ETHUMBQ_MAKE reply field, filled by the thumbnail process only on
     * FS3ETHUMBR_ERROR (BMFMT_UNKNOWN/meaningless on success): the sniffed
     * format (enum BmImageFormat, see bmimage.h) of fs3etm_SrcPath, so the
     * GUI can tell "unsupported format" (e.g. WebP with no webp.datatype
     * installed) apart from a corrupt download or a non-picture file --
     * see BmImage_SniffFormat(). */
    ULONG fs3etm_DetectedFormat;
} FS3EThumbMessage;

/* Start the thumbnail process. Returns the request MsgPort, or NULL on failure. */
struct MsgPort *FS3EThumb_Start(void);

/*
 * Ask the thumbnail process to shut down and wait for it to exit.
 * requestPort is the port returned by FS3EThumb_Start(); replyPort is a
 * temporary port created by the caller to receive the shutdown reply.
 */
void FS3EThumb_Stop(struct MsgPort *requestPort, struct MsgPort *replyPort);

/*
 * Ask the thumbnail process to generate a box-fit thumbnail of srcPath,
 * asynchronously -- returns immediately; the reply arrives later on
 * replyPort (the caller's own persistent port, checked from its Wait()
 * loop the same way as network_fs3e's netReplyPort). key is echoed back
 * in the reply (e.g. @acct, or a media URL for kind==FS3ETHUMB_KIND_MEDIA)
 * so the caller knows which entry to update; kind tells it which cache
 * pool that is (see enum FS3EThumbKind). cacheKeyPath/deleteSrcAfter are
 * forwarded straight to fs3etm_CacheKeyPath/fs3etm_DeleteSrcAfter (see
 * their doc comments above) -- pass NULL/FALSE for the common case where
 * srcPath is itself the persistent, sibling-naming-worthy location.
 * Returns FALSE (nothing queued, no allocation left behind) if
 * requestPort/srcPath are missing or srcPath/key/cacheKeyPath don't fit
 * the fixed-size fields.
 */
BOOL FS3EThumb_Request(struct MsgPort *requestPort, struct MsgPort *replyPort,
                        const char *srcPath, const char *key, ULONG kind,
                        const char *cacheKeyPath, BOOL deleteSrcAfter,
                        UWORD targetW, UWORD targetH);

#endif /* FS3ETHUMB_H */
