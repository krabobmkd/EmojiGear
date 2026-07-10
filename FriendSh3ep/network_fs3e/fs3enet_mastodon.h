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
 */
BOOL FS3EMastodon_VerifyCredentials(const char *apiBaseUrl, const char *accessToken,
                                   FS3EMastodonAccount *outAccount);

/*
 * GET /api/v1/{timeline} -- timeline is the full path relative to /api/v1/,
 * e.g. "timelines/home?limit=20" or "accounts/123/statuses?limit=20"
 * (a user's own toots). On success *outJson is a cJSON array of Status
 * objects owned by the caller, who must cJSON_Delete() it.
 */
BOOL FS3EMastodon_GetTimeline(const char *apiBaseUrl, const char *accessToken,
                             const char *timeline, cJSON **outJson);

/*
 * POST /api/v1/statuses - publishes a new status. On success fills
 * outStatusId (the new status' id, NUL-terminated) and returns TRUE.
 * visibility is one of "public", "unlisted", "private", "direct".
 */
BOOL FS3EMastodon_PostStatus(const char *apiBaseUrl, const char *accessToken,
                            const char *statusText, const char *visibility,
                            char *outStatusId, ULONG outStatusIdSize);

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
 * (followed_by, blocking, muting, ...) isn't used yet.
 */
BOOL FS3EMastodon_GetRelationship(const char *apiBaseUrl, const char *accessToken,
                                  const char *accountId, BOOL *outFollowing);

/*
 * POST /api/v1/accounts/:id/follow or .../unfollow. On success fills
 * outFollowing from the server's response (a Relationship object, which
 * carries no follower/following counts to accidentally trust -- same
 * "only the confirmed boolean" rule as FS3EMastodon_Favourite).
 */
BOOL FS3EMastodon_Follow(const char *apiBaseUrl, const char *accessToken,
                         const char *accountId, BOOL follow,
                         BOOL *outFollowing);

#endif /* FS3ENET_MASTODON_H */
