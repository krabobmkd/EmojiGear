#ifndef FS3ENET_MASTODON_H
#define FS3ENET_MASTODON_H

/*
 * FriendSh3ep network process - Mastodon REST API calls.
 *
 * Mirrors the sequence of calls brutaldon makes via Mastodon.py (see
 * ~/Code/Amiga/brutaldon/brutaldon/views.py and ../ARCHITECTURE.md section
 * 3/4): app registration, the out-of-band OAuth code exchange, account
 * verification, timeline fetch and posting a status.
 *
 * All functions are blocking and must be called from the network process
 * after FS3EHttp_Init(). apiBaseUrl is the instance's base URL, e.g.
 * "https://mastodon.social" (no trailing slash).
 */

#include <exec/types.h>

#include "cjson/cJSON.h"

/* Out-of-band redirect URI - see ARCHITECTURE.md section 4.4. The user
 * authorizes in any browser and is shown a code to paste back into
 * FriendSh3ep, so no local HTTP listener is needed. */
#define FS3EMASTODON_OOB_REDIRECT_URI "urn:ietf:wg:oauth:2.0:oob"

/* Scopes requested for the whole app. */
#define FS3EMASTODON_SCOPES "read write follow"

/* Account fields needed after login (see verify_credentials). Avatar URLs
 * point at images the GUI will fetch/cache separately (FS3ENETQ_FETCH_IMAGE,
 * Phase 2).
 *
 * All char * fields are either:
 *   a) individually AllocVec'd (temporary use inside FS3EMastodon_VerifyCredentials
 *      callers) — free with FS3EMastodonAccount_Free(); or
 *   b) pointing into a surrounding flat IPC block — do NOT call
 *      FS3EMastodonAccount_Free() on those; FreeVec the enclosing block instead.
 */
typedef struct FS3EMastodonAccount
{
    char *fma_Id;
    char *fma_Username;
    char *fma_Acct;
    char *fma_DisplayName;
    char *fma_AvatarURL;

    /* Profile-view fields -- unset ("" / 0) by FS3EMastodon_VerifyCredentials,
     * which has no reason to fetch them; only FS3EMastodon_LookupAccount
     * fills these in. */
    char  *fma_Note;            /* bio, HTML-stripped */
    ULONG  fma_FollowersCount;
    ULONG  fma_FollowingCount;
} FS3EMastodonAccount;

/* Frees each individually-AllocVec'd string field. Only call this on an
 * account whose fields were set by FS3EMastodon_VerifyCredentials() directly
 * (i.e. not part of a flat IPC block). */
void FS3EMastodonAccount_Free(FS3EMastodonAccount *acc);

/*
 * POST /api/v1/apps - register FriendSh3ep as an OAuth app on apiBaseUrl.
 * On success fills outClientId/outClientSecret (NUL-terminated) and
 * returns TRUE.
 */
BOOL FS3EMastodon_CreateApp(const char *apiBaseUrl, const char *clientName,
                           char *outClientId, ULONG clientIdSize,
                           char *outClientSecret, ULONG clientSecretSize);

/*
 * Builds the "GET /oauth/authorize?..." URL the user should open in a
 * browser to authorize FriendSh3ep. Does not perform any request.
 */
void FS3EMastodon_BuildAuthorizeURL(const char *apiBaseUrl, const char *clientId,
                                   char *outUrl, ULONG outUrlSize);

/*
 * POST /oauth/token - exchanges the code the user pasted back (after
 * authorizing via the URL from FS3EMastodon_BuildAuthorizeURL()) for an
 * access token. On success fills outAccessToken and returns TRUE.
 */
BOOL FS3EMastodon_ExchangeCode(const char *apiBaseUrl, const char *clientId,
                              const char *clientSecret, const char *code,
                              char *outAccessToken, ULONG outAccessTokenSize);

/*
 * GET /api/v1/accounts/verify_credentials - confirms accessToken is valid
 * and returns the logged-in account's basic info.
 *
 * outRejected (may be NULL if the caller doesn't care) is set TRUE only
 * when the server actually answered but the answer wasn't a valid account
 * (a Doorkeeper-style {"error":"The access token is invalid"} body, or any
 * other object/response missing "id") -- i.e. a confirmed rejection of
 * THIS token, not just "couldn't reach the server". Left FALSE (never set
 * TRUE) when the request didn't get a response at all (FS3EHttp_Get
 * itself failed -- DNS/connect/TLS failure, no internet, ...), since that
 * says nothing about the token's own validity. Always FALSE when this
 * function returns TRUE. Callers that need to tell "your token was
 * revoked" apart from "we're offline right now" (see
 * FS3EApp_VerifyStoredAccount() in fs3eaccounts.c) should pass a non-NULL
 * pointer; FS3ENET_HandleLoginFinish doesn't bother, both cases already
 * show the same "couldn't log in" message there.
 */
BOOL FS3EMastodon_VerifyCredentials(const char *apiBaseUrl, const char *accessToken,
                                   FS3EMastodonAccount *outAccount, BOOL *outRejected);

/*
 * GET /api/v1/{timeline} (or /api/v2/{timeline} -- see
 * FS3ENET_TLSHAPE_SEARCH_STATUSES below) -- timeline is the full path
 * relative to that, e.g. "timelines/home?limit=20",
 * "accounts/123/statuses?limit=20" (a user's own toots), "statuses/123"
 * (one status), "statuses/123/context" (that status' replies), or
 * "search?type=statuses&limit=20&q=..." (word/hashtag search). On success
 * *outJson is a cJSON array of Status objects owned by the caller, who
 * must cJSON_Delete() it -- regardless of responseShape, always a plain
 * array, never the raw shape the endpoint itself returned.
 *
 * responseShape is a `enum FS3ENetTimelineShape` value from fs3enet.h,
 * passed as a plain ULONG here since that header is the one including
 * this one, not the reverse (same reasoning TootTimeline keeps its own
 * TTL_MEDIA_KIND_* defines instead of including fs3enet.h directly):
 *   FS3ENET_TLSHAPE_ARRAY (0)        -- endpoint already returns Status[]
 *   FS3ENET_TLSHAPE_SINGLE (1)       -- endpoint returns one Status object;
 *                                        wrapped into a 1-element array
 *   FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS (2) -- endpoint returns
 *                                        {"ancestors":[...],"descendants":[...]};
 *                                        only descendants is unwrapped and
 *                                        returned, ancestors is discarded
 *   FS3ENET_TLSHAPE_SEARCH_STATUSES (3) -- GET /api/v2/{timeline} (not v1);
 *                                        endpoint returns
 *                                        {"accounts":[...],"statuses":[...],
 *                                        "hashtags":[...]}; only statuses is
 *                                        unwrapped and returned, the rest
 *                                        discarded -- word and hashtag
 *                                        search both go through this same
 *                                        shape/endpoint (Mastodon's own
 *                                        search treats a leading '#' in q
 *                                        as a hashtag match)
 *   FS3ENET_TLSHAPE_SINGLE_REFRESH (4)  -- identical wire shape to SINGLE
 *                                        (GET .../statuses/:id, one Status
 *                                        object); only the GUI's reply
 *                                        handling differs (F5 refresh patch
 *                                        vs. new-toot insert), so it's
 *                                        normalized the same way here
 *   FS3ENET_TLSHAPE_SEARCH_ACCOUNTS (5) -- GET /api/v2/{timeline} (not v1);
 *                                        same {"accounts":[...],"statuses":[...],
 *                                        "hashtags":[...]} wrapper as
 *                                        SEARCH_STATUSES, just unwrapping
 *                                        "accounts" instead -- used by
 *                                        FS3ENETQ_ACCOUNTS_LIST, not
 *                                        FS3ENETQ_TIMELINE
 *   FS3ENET_TLSHAPE_CONTEXT_ANCESTORS (6) -- same endpoint/JSON shape as
 *                                        CONTEXT_DESCENDANTS above, just
 *                                        unwrapping "ancestors" instead;
 *                                        appended last (not grouped next to
 *                                        CONTEXT_DESCENDANTS) so this file's
 *                                        own plain-int mirror of the enum
 *                                        stays numbered the same as
 *                                        fs3enet.h's real one
 *
 * outAuthRequired (may be NULL if the caller doesn't care) is set TRUE only
 * when the server answered but refused the request outright with
 * {"error":"This method requires an authenticated user"} -- Mastodon's
 * "timeline preview" admin setting turned off, which some instances now do
 * even for nominally-public endpoints like timelines/public (seen on
 * mastodon.social). Distinct from a plain parse/shape failure: this is a
 * confirmed, deliberate server policy, not a transient error -- see
 * FS3ENETR_AUTH_REQUIRED in fs3enet.h, which callers use it to select.
 * Left FALSE (never set TRUE) for every other failure. Always FALSE when
 * this function returns TRUE.
 */
BOOL FS3EMastodon_GetTimeline(const char *apiBaseUrl, const char *accessToken,
                             const char *timeline, ULONG responseShape,
                             cJSON **outJson, BOOL *outAuthRequired);

/* application/x-www-form-urlencoded percent-encoding -- see the definition
 * in fs3enet_mastodon.c for the exact rule. Exposed so FS3ENet_HandleTimeline
 * (fs3enet.c) can encode a search query's raw user text before folding it
 * into the timeline string GetTimeline above receives. */
void FS3EMastodon_UrlEncode(const char *src, char *dst, ULONG dstSize);

/*
 * POST /api/v1/statuses - publishes a new status. On success fills
 * outStatusId (the new status' id, NUL-terminated) and returns TRUE.
 * visibility is one of "public", "unlisted", "private", "direct".
 * sensitive maps straight to Mastodon's `sensitive` flag -- applies to the
 * whole status (and every attachment in it, if any); always sent, TRUE or
 * FALSE, not omitted (see FS3ETootView's sensitiveCheck in fs3etootview.h).
 * inReplyToId is the status this is a reply to, or NULL/"" for a
 * standalone toot -- this is what actually threads the reply on the
 * server (Mastodon also auto-notifies/mentions the parent's author from
 * this alone, regardless of whether the body text visibly @-mentions them).
 * quoteApprovalPolicy is one of "public"/"followers"/"nobody" (Mastodon
 * 4.5+'s quote_approval_policy -- who's allowed to quote this status; the
 * server silently ignores it on older instances that don't support quote
 * posts yet). Server also forces it to "nobody" itself when visibility is
 * private/direct, so no client-side special-casing needed here.
 * quotedStatusId, if non-NULL/non-empty, makes this a quote post of that
 * status (Mastodon 4.5+'s quoted_status_id) -- omitted from the request
 * entirely when NULL/"" rather than sent empty, since an empty string is
 * NOT the same as "no quote" to the server. The server itself enforces
 * whether quoting is actually allowed (target's quote_approval_policy,
 * follow relationship, visibility) -- see FS3ENetStatus.fmas_Quotable for
 * the read-side signal used to decide whether to even offer Quote in the
 * UI, but this call doesn't re-check any of that itself.
 * mediaIds/mediaCount, if mediaCount>0, attaches the given (already
 * uploaded -- see FS3EMastodon_UploadMedia) media ids to the new status,
 * same media_ids key FS3EMastodon_EditStatus sends; omitted entirely when
 * mediaCount==0, same "don't send an empty key" reasoning.
 */
BOOL FS3EMastodon_PostStatus(const char *apiBaseUrl, const char *accessToken,
                            const char *statusText, const char *visibility,
                            BOOL sensitive,
                            const char *inReplyToId,
                            const char *quoteApprovalPolicy,
                            const char *quotedStatusId,
                            const char *const *mediaIds, ULONG mediaCount,
                            char *outStatusId, ULONG outStatusIdSize);

/*
 * PUT /api/v1/statuses/:id -- edits an existing status' text (and, if
 * mediaCount>0, resends its existing media_ids so they survive the edit --
 * Mastodon's real server-side UpdateStatusService only touches attachments
 * "if @options.key?(:media_ids)" at all, so omitting the key here when
 * mediaCount==0 is correct, not just "empty"). Does NOT send visibility --
 * Mastodon's edit endpoint doesn't accept changing it. Success = the
 * server responded 200 OK; unlike FS3EMastodon_PostStatus this doesn't
 * need to infer success from a parsed field, since FS3EHttp_Put() (unlike
 * Get/Post) exposes a real HTTP status code.
 */
BOOL FS3EMastodon_EditStatus(const char *apiBaseUrl, const char *accessToken,
                            const char *statusId, const char *statusText,
                            const char *const *mediaIds, ULONG mediaCount);

/*
 * PATCH /api/v1/accounts/update_credentials -- sets the connected user's
 * own bio (Mastodon's "note" field on that endpoint; nothing else about
 * the account is touched -- avatar/display name/etc. aren't wired up
 * here). Only PATCH-capable of the raw-BIO verbs (see FS3EHttp_Patch),
 * same reasoning as FS3EMastodon_EditStatus's PUT.
 *
 * On success, outNote is filled with the server's echoed note text --
 * RAW HTML, same convention as FS3EMastodonAccount.fma_Note/
 * FS3EMastodon_LookupAccount (the caller is responsible for stripping it
 * before showing it anywhere, e.g. FS3ENet_HandleUpdateBio in fs3enet.c) --
 * and returns TRUE. Reading it back from the response (rather than just
 * trusting the request's own `note` text) matters because Mastodon may
 * normalize it (e.g. collapsing blank lines) server-side.
 */
BOOL FS3EMastodon_UpdateBio(const char *apiBaseUrl, const char *accessToken,
                            const char *note,
                            char *outNote, ULONG outNoteSize);

/*
 * DELETE /api/v1/statuses/:id -- deletes an existing status (own toots
 * only). No request body. Success = the server responded 200 OK, same
 * "real status code, not a parsed field" reasoning as FS3EMastodon_EditStatus.
 */
BOOL FS3EMastodon_DeleteStatus(const char *apiBaseUrl, const char *accessToken,
                              const char *statusId);

/*
 * POST /api/v2/media -- uploads fileBytes/fileLen as a new media
 * attachment, multipart/form-data (the only encoding this endpoint
 * accepts), a single "file" part named fileName with Content-Type
 * mimeType. On success fills outMediaId (the new attachment's id, needed
 * by a following FS3EMastodon_PostStatus's mediaIds) and returns TRUE.
 *
 * Doesn't poll for processing to finish itself: Mastodon returns 200
 * immediately for ordinary images (processing is synchronous server-side
 * for those), but can return 202 Accepted for audio/video still being
 * transcoded -- the id is usable right away but attaching it to a status
 * before processing finishes 422s. Callers uploading audio/video should
 * follow a successful call with FS3EMastodon_WaitMediaReady() before
 * attaching outMediaId to a status -- see FS3ENet_HandleUploadMedia
 * (fs3enet.c) for the one caller that does.
 */
BOOL FS3EMastodon_UploadMedia(const char *apiBaseUrl, const char *accessToken,
                              const void *fileBytes, ULONG fileLen,
                              const char *fileName, const char *mimeType,
                              char *outMediaId, ULONG outMediaIdSize);

/*
 * GET /api/v1/media/:id, polled in a loop (bounded attempts, ~1s apart via
 * dos.library's Delay()) until the server answers 200 (fully processed)
 * instead of 206 (still transcoding). Returns TRUE once ready; FALSE if
 * the poll budget runs out, the server answers with anything other than
 * 200/206, or the request fails outright.
 */
BOOL FS3EMastodon_WaitMediaReady(const char *apiBaseUrl, const char *accessToken,
                                 const char *mediaId);

/*
 * POST /api/v1/statuses/:id/favourite or .../unfavourite. On success fills
 * outFavourited from the server's response and returns TRUE.
 *
 * Deliberately does NOT also hand back replies_count/reblogs_count/
 * favourites_count from that same response: those are easy to assume are
 * always present on the full Status object this endpoint returns, but
 * aren't reliably so in practice (seen in the wild: an instance whose
 * favourite/unfavourite response carries a trimmed-down status missing
 * those counters) -- a caller that blindly copied them across would
 * silently stomp this toot's OTHER counts (Reply/Boost) with zeros on
 * every single favourite toggle. The favourited boolean is the one thing
 * this call needs confirmed from the server (the request could be
 * rejected, or already be in that state); the resulting favourites_count
 * is a local +1/-1 delta the caller applies itself -- see
 * TTIMELINE_UpdatePost/TTL_POSTUPD_FAVOURITED in fs3etoottimeline.h.
 */
BOOL FS3EMastodon_Favourite(const char *apiBaseUrl, const char *accessToken,
                           const char *statusId, BOOL favourite,
                           BOOL *outFavourited);

/*
 * POST /api/v1/statuses/:id/reblog or .../unreblog. On success fills
 * outReblogged from the server's response and returns TRUE. Same "only the
 * confirmed boolean, never the echoed counts" reasoning as
 * FS3EMastodon_Favourite above.
 */
BOOL FS3EMastodon_Reblog(const char *apiBaseUrl, const char *accessToken,
                         const char *statusId, BOOL reblog,
                         BOOL *outReblogged);

/*
 * GET /api/v1/accounts/lookup?acct=<acct> -- resolves an acct string
 * ("user" or "user@instance", no leading '@') to a full account. The entry
 * point for opening a profile view: nothing else carries an account id,
 * only an acct string scraped off a toot's author or an @mention token.
 * outAccount->fma_Note is the RAW (HTML) bio -- unlike VerifyCredentials,
 * which has no caller that cares, this one's caller (fs3enet.c) strips it
 * the same way toot content already is, via the same StripHTML() helper;
 * doing that here would need HTML-stripping logic duplicated into this
 * file, which is otherwise pure "talk to the Mastodon API", not text
 * processing.
 */
BOOL FS3EMastodon_LookupAccount(const char *apiBaseUrl, const char *accessToken,
                                const char *acct, FS3EMastodonAccount *outAccount);

/*
 * GET /api/v1/accounts/relationships?id[]=<accountId> -- only the
 * connected user's own "following" state is needed (profile view's
 * Follow/Unfollow button label); the rest of the Relationship object
 * (blocking, muting, ...) isn't used yet. See FS3EMastodon_GetRelationships
 * (plural) below for the batch form, which also reads followed_by.
 */
BOOL FS3EMastodon_GetRelationship(const char *apiBaseUrl, const char *accessToken,
                                  const char *accountId, BOOL *outFollowing);

/*
 * GET /api/v1/accounts/relationships?id[]=<id>&id[]=<id>... -- batch form
 * of FS3EMastodon_GetRelationship above, one repeated id[] per account, for
 * badging account-row list items ("Follows you") without one HTTP round
 * trip per row. Unlike the singular form, this one also reads followed_by
 * -- the singular caller only ever needs the connected user's OWN following
 * state, but a list of other accounts needs to know if THEY follow back.
 * *outJson is the raw parsed Relationship array (caller walks it and
 * cJSON_Delete()s it), not pre-extracted into out-params, since the caller
 * needs to match each entry back to its account id itself.
 */
BOOL FS3EMastodon_GetRelationships(const char *apiBaseUrl, const char *accessToken,
                                   const char *const *accountIds, ULONG count,
                                   cJSON **outJson);

/*
 * POST /api/v1/accounts/:id/follow or .../unfollow. On success fills
 * outFollowing from the server's response (a Relationship object, which
 * carries no follower/following counts to accidentally trust -- same
 * "only the confirmed boolean" rule as FS3EMastodon_Favourite).
 */
BOOL FS3EMastodon_Follow(const char *apiBaseUrl, const char *accessToken,
                         const char *accountId, BOOL follow,
                         BOOL *outFollowing);

/* Mastodon's own historical per-toot character limit -- used as
 * outMaxChars' fallback value by FS3EMastodon_GetInstanceInfo() when
 * neither the v2 nor v1 instance endpoint hands back a usable number, so
 * callers never have to special-case "unknown". */
#define FS3EMASTODON_DEFAULT_MAX_CHARS 500

/*
 * GET /api/v2/instance -- reads configuration.statuses.max_characters,
 * the server's per-toot character limit (varies per instance: some raise
 * it well past Mastodon's 500 default, e.g. many Fediverse forks default
 * to a few thousand). Falls back to GET /api/v1/instance's legacy
 * top-level max_toot_chars field (older Mastodon versions, some forks)
 * if v2 is unreachable or doesn't carry the field, and to
 * FS3EMASTODON_DEFAULT_MAX_CHARS if neither does.
 *
 * outMaxChars is always set to something usable. Returns TRUE only if a
 * real value came back from the server (FALSE means outMaxChars is the
 * fallback default) -- callers that don't care about that distinction can
 * ignore the return value and just use outMaxChars either way.
 * No accessToken needed; both endpoints are public.
 */
BOOL FS3EMastodon_GetInstanceInfo(const char *apiBaseUrl, ULONG *outMaxChars);

#endif /* FS3ENET_MASTODON_H */
