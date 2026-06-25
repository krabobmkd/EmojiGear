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
 * GET /api/v1/timelines/{timeline} (e.g. "home" or "public"). On success
 * *outJson is a cJSON array of Status objects owned by the caller, who
 * must cJSON_Delete() it.
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

#endif /* FS3ENET_MASTODON_H */
