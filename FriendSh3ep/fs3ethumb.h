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

/* What fs3etmr_Key/fs3etmy_Key identifies and which live-cache pool the
 * reply belongs in -- set by the caller in FS3EThumbMakeReq, echoed back
 * into FS3EThumbMakeReply by FS3EThumb_HandleMake (see FS3EThumb_Request). */
enum FS3EThumbKind
{
    FS3ETHUMB_KIND_AVATAR = 0,  /* Key is an @acct */
    FS3ETHUMB_KIND_MEDIA  = 1,  /* Key is the attachment's preview/URL */
    FS3ETHUMB_KIND_CARD   = 2   /* Key is a link preview card's image URL --
                                 * this process itself never branches on Kind
                                 * (width/height are separate explicit args below), it's
                                 * purely an echo the GUI uses to route the reply to the
                                 * right AvatarImages_*ThumbReady pool (see
                                 * FS3EApp_HandleThumbReply) */
};

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

/* Raw-decode (fs3etmr_RawDecode TRUE, see below) size cap: comfortably
 * above FS3ETHUMB_MEDIA_WIDTH/HEIGHT_CAP so the "don't minify" setting
 * still visibly wins on quality, well below an arbitrary camera-original
 * resolution -- see BmImage_ReadSourceRgbCapped's doc comment for the
 * single-halving rule this enforces. Shared by MEDIA and CARD raw
 * decodes, same as they already share FS3ETHUMB_MEDIA_WIDTH/HEIGHT_CAP
 * for the minified path. */
#define FS3ETHUMB_MEDIA_RAW_MAX_W  1024
#define FS3ETHUMB_MEDIA_RAW_MAX_H  1024

/*
 * Header-plus-payload message, same shape network_fs3e/fs3enet.h's
 * FS3ENetMessage already uses: fs3etm_Data is a separate AllocVec'd block
 * holding an FS3EThumbMakeReq while the request is in flight to the
 * thumbnail process, then freed and replaced with a freshly AllocVec'd
 * FS3EThumbMakeReply once handled (see FS3EThumb_HandleMake) -- not
 * appended to, not reused in place. Neither payload struct bounds its path/
 * key strings to a fixed size: each is a packed variable-length string
 * living right after the struct's fixed fields, addressed by a char*
 * pointing into that same allocation (see FS3EThumb_PackStr in
 * fs3ethumb.c). fs3etm_Data is NULL for FS3ETHUMBQ_SHUTDOWN, which carries
 * no payload.
 */
typedef struct FS3EThumbMessage
{
    struct Message fs3etm_Msg;
    ULONG          fs3etm_Type;    /* enum FS3EThumbRequestType */
    ULONG          fs3etm_Result;  /* enum FS3EThumbResult, set on reply */
    void          *fs3etm_Data;    /* FS3EThumbMakeReq, then FS3EThumbMakeReply -- see above */
    ULONG          fs3etm_DataLen;
} FS3EThumbMessage;

/* FS3ETHUMBQ_MAKE request payload -- fs3etm_Data while a request is in
 * flight to the thumbnail process. Built by FS3EThumb_Request(). */
typedef struct FS3EThumbMakeReq
{
    ULONG fs3etmr_Kind;    /* enum FS3EThumbKind; echoed back in the reply */
    UWORD fs3etmr_TargetW;
    UWORD fs3etmr_TargetH;
    /* TRUE = delete fs3etmr_SrcPath once this request is handled (success
     * or failure) -- set when the caller downloaded it to RAM:T rather
     * than the persistent cache (see FS3ENetFetchImageReply.fs3enf_IsTemp
     * and the "Keep big user icons/thumbnails" settings). */
    BOOL  fs3etmr_DeleteSrcAfter;
    /* TRUE = decode SrcPath at native size (capped -- see
     * FS3ETHUMB_MEDIA_RAW_MAX_W/H -- via BmImage_ReadSourceRgbCapped) and
     * hand the raw RGB24 pixels back in the reply instead of writing a
     * scaled BMP to disk; fs3etmr_TargetW/H are ignored in this mode. Set
     * when app->settings.minifyThumbnails is FALSE for MEDIA/CARD kinds
     * -- see FS3EThumbMakeReply.fs3etmy_RawPixels for the reply side.
     * Avatars always minify; never set for FS3ETHUMB_KIND_AVATAR. */
    BOOL  fs3etmr_RawDecode;

    char *fs3etmr_SrcPath;      /* packed string -- source image to decode/scale */
    char *fs3etmr_Key;          /* packed string; echoed back, e.g. @acct or a media URL */

    /* Deterministic path (no extension) the resulting thumbnail should be
     * named/found after, independent of where fs3etmr_SrcPath actually
     * lives -- passed straight through to BmImage_GenerateScaledBmp's
     * cacheKeyPath (see bmimage.h). Empty = derive the name from SrcPath
     * itself (the common case: source and thumbnail are siblings). Needed
     * when SrcPath is a transient RAM:T download (see fs3etmr_DeleteSrcAfter)
     * but the thumbnail itself must still get a name stable across runs --
     * see FS3ENetFetchImageReply.fs3enf_CachePath, which is exactly this
     * path for the source URL. */
    char *fs3etmr_CacheKeyPath; /* packed string; "" = derive from SrcPath */
} FS3EThumbMakeReq;

/* FS3ETHUMBQ_MAKE reply payload -- replaces fs3etm_Data once
 * FS3EThumb_HandleMake() (or the shutdown-time abandon path) finishes. */
typedef struct FS3EThumbMakeReply
{
    ULONG fs3etmy_Kind; /* echoed from the request */

    /* Filled only on FS3ETHUMBR_ERROR (BMFMT_UNKNOWN/meaningless on
     * success): the sniffed format (enum BmImageFormat, see bmimage.h) of
     * fs3etmy_SrcPath, so the GUI can tell "unsupported format" (e.g. WebP
     * with no webp.datatype installed) apart from a corrupt download or a
     * non-picture file -- see BmImage_SniffFormat(). */
    ULONG fs3etmy_DetectedFormat;

    char *fs3etmy_Key;       /* packed string; echoed from the request */
    char *fs3etmy_SrcPath;   /* packed string; echoed from the request, for error logging */

    /* Deterministic ("<CacheKeyPath or SrcPath>.<TargetW>x<TargetH>.bmp")
     * so the GUI could derive it itself, but it's handed back anyway to
     * keep the reply self-contained. Empty on FS3ETHUMBR_ERROR, and
     * always empty on a fs3etmr_RawDecode reply (see fs3etmy_RawPixels
     * below) -- nothing was ever written to disk in that mode. */
    char *fs3etmy_ThumbPath; /* packed string */

    /* Best-effort AllocVec'd, top-down RGB24 (3 bytes/pixel, tightly
     * packed rows) buffer at fs3etmy_RawWidth x fs3etmy_RawHeight, so the
     * GUI task never has to touch a file for this reply at all -- set
     * whenever the thumbnail process could hand back ready pixels
     * regardless of Kind or fs3etmr_RawDecode:
     *   - fs3etmr_RawDecode TRUE (MEDIA/CARD, minifyThumbnails FALSE):
     *     the decoded-at-native-size (capped) pixels themselves -- see
     *     FS3EThumb_HandleMakeRaw. fs3etmy_ThumbPath is empty in this
     *     case, nothing was ever written to disk.
     *   - fs3etmr_RawDecode FALSE (any Kind, the minified path): the
     *     thumbnail process reads its own just-(re)confirmed BMP output
     *     back with RgbImage_LoadBmp() before replying -- see
     *     FS3EThumb_HandleMake. fs3etmy_ThumbPath is still filled in this
     *     case too, as a fallback the GUI can load itself the old way if
     *     this field is ever NULL (that read failing right after this
     *     process just wrote/confirmed the exact same file would be very
     *     unusual, but isn't treated as a hard error).
     * NOT part of this struct's own packed-string allocation (it can be
     * large, and isn't a string) -- a separate AllocVec block whose
     * pointer just happens to travel inside this reply. Safe to hand
     * across the task boundary as a bare pointer because this process
     * shares the main executable's flat address space (see fs3ethumb.h's
     * file header comment) -- no copy needed, unlike everything else in
     * this struct. NULL on error, or on a minified success where the
     * read-back above failed. Ownership: whoever reads it
     * (FS3EApp_HandleThumbReply, via RgbImage_AdoptBuffer -- rgbimage.h)
     * takes it over and must NULL this field out immediately after, so
     * FS3EThumb_FreeMessage() still frees it as a safety net for a reply
     * that's dropped without ever being adopted. */
    UBYTE *fs3etmy_RawPixels;
    UWORD  fs3etmy_RawWidth;
    UWORD  fs3etmy_RawHeight;
} FS3EThumbMakeReply;

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
 * forwarded straight to fs3etmr_CacheKeyPath/fs3etmr_DeleteSrcAfter (see
 * their doc comments above) -- pass NULL/FALSE for the common case where
 * srcPath is itself the persistent, sibling-naming-worthy location.
 * rawDecode is forwarded straight to fs3etmr_RawDecode -- pass TRUE only
 * for MEDIA/CARD when app->settings.minifyThumbnails is FALSE; targetW/
 * targetH are ignored by the thumbnail process in that case (pass 0,0
 * for clarity), the FS3ETHUMB_MEDIA_RAW_MAX_W/H cap applies instead. No
 * length limit on srcPath/key/cacheKeyPath -- each is packed to its exact
 * size (see FS3EThumbMakeReq). Returns FALSE (nothing queued, no
 * allocation left behind) if requestPort/srcPath are missing or an
 * allocation fails.
 */
BOOL FS3EThumb_Request(struct MsgPort *requestPort, struct MsgPort *replyPort,
                        const char *srcPath, const char *key, ULONG kind,
                        const char *cacheKeyPath, BOOL deleteSrcAfter,
                        UWORD targetW, UWORD targetH, BOOL rawDecode);

/*
 * Frees a drained FS3EThumbMessage completely, including fs3etm_Data (an
 * FS3EThumbMakeReply by the time the GUI sees it -- a separate AllocVec'd
 * block now that fs3etm_Data replaced the old fixed-size fields, unlike
 * before when a bare FreeVec(msg) was enough) AND, if still non-NULL,
 * that reply's fs3etmy_RawPixels -- a safety net for a raw-decode reply
 * that never got adopted (see fs3etmy_RawPixels's doc comment); the
 * normal path already NULLed it out by the time this runs, so this is a
 * no-op then. Callers that used to FreeVec() a drained message directly
 * must call this instead. msg may be NULL.
 */
void FS3EThumb_FreeMessage(FS3EThumbMessage *msg);

#endif /* FS3ETHUMB_H */
