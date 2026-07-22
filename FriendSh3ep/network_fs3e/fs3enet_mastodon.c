/*
 * FriendSh3ep network process - Mastodon REST API calls.
 *
 * See fs3enet_mastodon.h for the public API and ../ARCHITECTURE.md section 3
 * for the brutaldon call sequence this mirrors.
 */

#include "fs3enet_mastodon.h"
#include "fs3enet_http.h"

#include <stdio.h>
#include <string.h>

#include <exec/memory.h>
#include <proto/exec.h>

static const char FS3EMASTODON_HEX[] = "0123456789ABCDEF";

/* application/x-www-form-urlencoded percent-encoding: unreserved chars
 * pass through, space becomes '+', everything else is %XX. Not static:
 * FS3ENet_HandleTimeline (fs3enet.c, same network process, different
 * translation unit) also calls this directly, to encode
 * FS3ENetTimelineReq.fs3et_SearchQuery's raw user text for the
 * FS3ENET_TLSHAPE_SEARCH_STATUSES shape's q= param -- see its comment in
 * fs3enet.h. */
void FS3EMastodon_UrlEncode(const char *src, char *dst, ULONG dstSize)
{
    ULONG di = 0;

    for (; *src && di + 4 < dstSize; src++)
    {
        unsigned char c = (unsigned char)*src;

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
        {
            dst[di++] = (char)c;
        }
        else if (c == ' ')
        {
            dst[di++] = '+';
        }
        else
        {
            dst[di++] = '%';
            dst[di++] = FS3EMASTODON_HEX[c >> 4];
            dst[di++] = FS3EMASTODON_HEX[c & 0x0F];
        }
    }

    dst[di] = '\0';
}

/* Copies the string value of obj[key] into dst (NUL-terminated, truncated
 * to dstSize); dst is "" if the key is missing or not a string. */
static void FS3EMastodon_CopyJsonString(const cJSON *obj, const char *key,
                                       char *dst, ULONG dstSize)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);

    dst[0] = '\0';

    if (item && cJSON_IsString(item) && item->valuestring)
    {
        strncpy(dst, item->valuestring, dstSize - 1);
        dst[dstSize - 1] = '\0';
    }
}

/* AllocVec duplicate of obj[key]; caller must FreeVec() result.
 * Returns NULL if the key is absent or not a string. */
static char *FS3EMastodon_DupJsonString(const cJSON *obj, const char *key)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    ULONG len;
    char *dup;

    if (!item || !cJSON_IsString(item) || !item->valuestring)
        return NULL;

    len = (ULONG)(strlen(item->valuestring) + 1);
    dup = (char *)AllocVec(len, MEMF_ANY);
    if (dup) CopyMem(item->valuestring, dup, len);
    return dup;
}

void FS3EMastodonAccount_Free(FS3EMastodonAccount *acc)
{
    if (!acc) return;
    if (acc->fma_Id)          { FreeVec(acc->fma_Id);          acc->fma_Id          = NULL; }
    if (acc->fma_Username)    { FreeVec(acc->fma_Username);    acc->fma_Username    = NULL; }
    if (acc->fma_Acct)        { FreeVec(acc->fma_Acct);        acc->fma_Acct        = NULL; }
    if (acc->fma_DisplayName) { FreeVec(acc->fma_DisplayName); acc->fma_DisplayName = NULL; }
    if (acc->fma_AvatarURL)   { FreeVec(acc->fma_AvatarURL);   acc->fma_AvatarURL   = NULL; }
    if (acc->fma_Note)        { FreeVec(acc->fma_Note);        acc->fma_Note        = NULL; }
}

static void FS3EMastodon_BuildAuthHeader(char *dst, ULONG dstSize, const char *accessToken)
{
    snprintf(dst, dstSize, "Bearer %s", accessToken);
}

BOOL FS3EMastodon_CreateApp(const char *apiBaseUrl, const char *clientName,
                           char *outClientId, ULONG clientIdSize,
                           char *outClientSecret, ULONG clientSecretSize)
{
    char url[256];
    char body[512];
    char encName[128];
    char encRedirect[128];
    char encScopes[64];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    FS3EMastodon_UrlEncode(clientName, encName, sizeof(encName));
    FS3EMastodon_UrlEncode(FS3EMASTODON_OOB_REDIRECT_URI, encRedirect, sizeof(encRedirect));
    FS3EMastodon_UrlEncode(FS3EMASTODON_SCOPES, encScopes, sizeof(encScopes));

    snprintf(body, sizeof(body),
        "client_name=%s&redirect_uris=%s&scopes=%s&website=",
        encName, encRedirect, encScopes);

    snprintf(url, sizeof(url), "%s/api/v1/apps", apiBaseUrl);
    if (!FS3EHttp_Post(url, NULL, "application/x-www-form-urlencoded",
                     body, strlen(body), &resp))
        return FALSE;

    json = cJSON_Parse((char *)resp.fhr_Body);
    if (json)
    {
        FS3EMastodon_CopyJsonString(json, "client_id", outClientId, clientIdSize);
        FS3EMastodon_CopyJsonString(json, "client_secret", outClientSecret, clientSecretSize);

        ok = (outClientId[0] != '\0' && outClientSecret[0] != '\0');

        cJSON_Delete(json);
    }

    FS3EHttp_FreeResponse(&resp);

    return ok;
}

void FS3EMastodon_BuildAuthorizeURL(const char *apiBaseUrl, const char *clientId,
                                   char *outUrl, ULONG outUrlSize)
{
    char encRedirect[128];
    char encScopes[64];

    FS3EMastodon_UrlEncode(FS3EMASTODON_OOB_REDIRECT_URI, encRedirect, sizeof(encRedirect));
    FS3EMastodon_UrlEncode(FS3EMASTODON_SCOPES, encScopes, sizeof(encScopes));

    snprintf(outUrl, outUrlSize,
        "%s/oauth/authorize?response_type=code&client_id=%s&redirect_uri=%s&scope=%s",
        apiBaseUrl, clientId, encRedirect, encScopes);
}

BOOL FS3EMastodon_ExchangeCode(const char *apiBaseUrl, const char *clientId,
                              const char *clientSecret, const char *code,
                              char *outAccessToken, ULONG outAccessTokenSize)
{
    char url[256];
    char body[1024];
    char encRedirect[128];
    char encScopes[64];
    char encCode[256];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    FS3EMastodon_UrlEncode(FS3EMASTODON_OOB_REDIRECT_URI, encRedirect, sizeof(encRedirect));
    FS3EMastodon_UrlEncode(FS3EMASTODON_SCOPES, encScopes, sizeof(encScopes));
    FS3EMastodon_UrlEncode(code, encCode, sizeof(encCode));

    snprintf(body, sizeof(body),
        "client_id=%s&client_secret=%s&redirect_uri=%s"
        "&grant_type=authorization_code&code=%s&scope=%s",
        clientId, clientSecret, encRedirect, encCode, encScopes);

    snprintf(url, sizeof(url), "%s/oauth/token", apiBaseUrl);

    if (!FS3EHttp_Post(url, NULL, "application/x-www-form-urlencoded",
                     body, strlen(body), &resp))
        return FALSE;

    json = cJSON_Parse((char *)resp.fhr_Body);
    if (json)
    {
        FS3EMastodon_CopyJsonString(json, "access_token", outAccessToken, outAccessTokenSize);

        ok = (outAccessToken[0] != '\0');

        cJSON_Delete(json);
    }

    FS3EHttp_FreeResponse(&resp);

    return ok;
}

BOOL FS3EMastodon_VerifyCredentials(const char *apiBaseUrl, const char *accessToken,
                                   FS3EMastodonAccount *outAccount)
{
    char url[256];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    snprintf(url, sizeof(url), "%s/api/v1/accounts/verify_credentials", apiBaseUrl);
    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    if (!FS3EHttp_Get(url, headers, &resp))
        return FALSE;

    json = cJSON_Parse((char *)resp.fhr_Body);
    if (json)
    {
        outAccount->fma_Id          = FS3EMastodon_DupJsonString(json, "id");
        outAccount->fma_Username    = FS3EMastodon_DupJsonString(json, "username");
        outAccount->fma_Acct        = FS3EMastodon_DupJsonString(json, "acct");
        outAccount->fma_DisplayName = FS3EMastodon_DupJsonString(json, "display_name");
        outAccount->fma_AvatarURL   = FS3EMastodon_DupJsonString(json, "avatar");

        ok = (outAccount->fma_Id != NULL);

        cJSON_Delete(json);
    }

    FS3EHttp_FreeResponse(&resp);

    return ok;
}

BOOL FS3EMastodon_GetInstanceInfo(const char *apiBaseUrl, ULONG *outMaxChars)
{
    char url[256];
    FS3EHttpHeader headers[1];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    *outMaxChars = FS3EMASTODON_DEFAULT_MAX_CHARS;

    headers[0].fhh_Name  = NULL;
    headers[0].fhh_Value = NULL;

    /* v2 first: configuration.statuses.max_characters -- current Mastodon
     * and most compatible forks. */
    snprintf(url, sizeof(url), "%s/api/v2/instance", apiBaseUrl);
    if (FS3EHttp_Get(url, headers, &resp))
    {
        json = cJSON_Parse((char *)resp.fhr_Body);
        if (json)
        {
            const cJSON *config   = cJSON_GetObjectItemCaseSensitive(json, "configuration");
            const cJSON *statuses = config ? cJSON_GetObjectItemCaseSensitive(config, "statuses") : NULL;
            const cJSON *maxChars = statuses ? cJSON_GetObjectItemCaseSensitive(statuses, "max_characters") : NULL;

            if (maxChars && cJSON_IsNumber(maxChars) && maxChars->valueint > 0)
            {
                *outMaxChars = (ULONG)maxChars->valueint;
                ok = TRUE;
            }
            cJSON_Delete(json);
        }
        FS3EHttp_FreeResponse(&resp);
    }

    if (ok) return TRUE;

    /* Fallback: older/legacy GET /api/v1/instance, top-level
     * max_toot_chars -- a non-standard field some Mastodon versions/
     * forks exposed before v2's nested configuration existed. */
    snprintf(url, sizeof(url), "%s/api/v1/instance", apiBaseUrl);
    if (FS3EHttp_Get(url, headers, &resp))
    {
        json = cJSON_Parse((char *)resp.fhr_Body);
        if (json)
        {
            const cJSON *maxChars = cJSON_GetObjectItemCaseSensitive(json, "max_toot_chars");

            if (maxChars && cJSON_IsNumber(maxChars) && maxChars->valueint > 0)
            {
                *outMaxChars = (ULONG)maxChars->valueint;
                ok = TRUE;
            }
            cJSON_Delete(json);
        }
        FS3EHttp_FreeResponse(&resp);
    }

    return ok;
}

/* Mirrors enum FS3ENetTimelineShape from fs3enet.h as plain ints -- see
 * FS3EMastodon_GetTimeline's header comment in fs3enet_mastodon.h for why
 * this file can't include that enum's own header (fs3enet.h includes
 * fs3enet_mastodon.h, not the reverse). */
#define FS3ENET_TLSHAPE_ARRAY               0
#define FS3ENET_TLSHAPE_SINGLE              1
#define FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS 2
#define FS3ENET_TLSHAPE_SEARCH_STATUSES     3
#define FS3ENET_TLSHAPE_SINGLE_REFRESH      4
#define FS3ENET_TLSHAPE_SEARCH_ACCOUNTS     5

BOOL FS3EMastodon_GetTimeline(const char *apiBaseUrl, const char *accessToken,
                             const char *timeline, ULONG responseShape,
                             cJSON **outJson)
{
    char url[512];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *json;

    *outJson = NULL;

    /* "timeline" already carries the full path for the SINGLE/CONTEXT_
     * DESCENDANTS/SEARCH_STATUSES shapes too (e.g. "statuses/123",
     * "statuses/123/context", "search?type=statuses&limit=20&q=...") --
     * callers pass whatever's needed relative to the API root, same as
     * every other shape. Search alone is a /api/v2/ endpoint, not v1. */
    if (responseShape == FS3ENET_TLSHAPE_SEARCH_STATUSES ||
        responseShape == FS3ENET_TLSHAPE_SEARCH_ACCOUNTS)
        snprintf(url, sizeof(url), "%s/api/v2/%s", apiBaseUrl, timeline);
    else
        snprintf(url, sizeof(url), "%s/api/v1/%s", apiBaseUrl, timeline);

    if (accessToken && accessToken[0]) {
        FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);
        headers[0].fhh_Name  = "Authorization";
        headers[0].fhh_Value = authHeader;
        headers[1].fhh_Name  = NULL;
        headers[1].fhh_Value = NULL;
    } else {
        headers[0].fhh_Name  = NULL;
        headers[0].fhh_Value = NULL;
    }


    if (!FS3EHttp_Get(url, headers, &resp)) {
        return FALSE;
    }


    json = cJSON_Parse((char *)resp.fhr_Body);

    /* Normalize the two non-array shapes into a plain array before the
     * generic "!cJSON_IsArray" check below -- see this function's header
     * comment in fs3enet_mastodon.h. SINGLE_REFRESH is the exact same wire
     * shape as SINGLE (GET .../statuses/:id, one Status object) -- only its
     * GUI-side meaning differs (F5 refresh, patch in place, vs. a toot
     * being newly inserted) -- so it's normalized identically here. */
    if (json && cJSON_IsObject(json) &&
        (responseShape == FS3ENET_TLSHAPE_SINGLE ||
         responseShape == FS3ENET_TLSHAPE_SINGLE_REFRESH))
    {
        cJSON *wrapper = cJSON_CreateArray();
        if (wrapper && cJSON_AddItemToArray(wrapper, json)) {
            json = wrapper; /* json (the single status object) is now owned by wrapper */
        } else {
            if (wrapper) cJSON_Delete(wrapper);
            cJSON_Delete(json);
            json = NULL;
        }
    }
    else if (json && cJSON_IsObject(json) && responseShape == FS3ENET_TLSHAPE_CONTEXT_DESCENDANTS)
    {
        cJSON *descendants = cJSON_DetachItemFromObjectCaseSensitive(json, "descendants");
        cJSON_Delete(json); /* frees the wrapper object + ancestors; descendants already detached, survives */
        json = descendants;
    }
    else if (json && cJSON_IsObject(json) && responseShape == FS3ENET_TLSHAPE_SEARCH_STATUSES)
    {
        cJSON *statuses = cJSON_DetachItemFromObjectCaseSensitive(json, "statuses");
        cJSON_Delete(json); /* frees the wrapper object + accounts/hashtags; statuses already detached, survives */
        json = statuses;
    }
    else if (json && cJSON_IsObject(json) && responseShape == FS3ENET_TLSHAPE_SEARCH_ACCOUNTS)
    {
        cJSON *accounts = cJSON_DetachItemFromObjectCaseSensitive(json, "accounts");
        cJSON_Delete(json); /* frees the wrapper object + statuses/hashtags; accounts already detached, survives */
        json = accounts;
    }

    if (!json || !cJSON_IsArray(json))
    {
        if (!json) {
            const char *errptr = cJSON_GetErrorPtr();
        } else {
            cJSON_Delete(json);
            json = NULL;
        }
        /* Print first 200 bytes of the body for context */
        if (resp.fhr_Body) {
            char preview[201];
            ULONG plen = resp.fhr_BodyLen < 200 ? resp.fhr_BodyLen : 200;
            CopyMem(resp.fhr_Body, preview, plen);
            preview[plen] = '\0';
        }
        FS3EHttp_FreeResponse(&resp);
        return FALSE;
    }

    FS3EHttp_FreeResponse(&resp);
    *outJson = json;
    return TRUE;
}

BOOL FS3EMastodon_PostStatus(const char *apiBaseUrl, const char *accessToken,
                            const char *statusText, const char *visibility,
                            const char *inReplyToId,
                            char *outStatusId, ULONG outStatusIdSize)
{
    char url[256];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *reqJson, *json;
    char *reqBody;
    BOOL ok = FALSE;

    reqJson = cJSON_CreateObject();
    if (!reqJson)
        return FALSE;

    cJSON_AddStringToObject(reqJson, "status", statusText);
    cJSON_AddStringToObject(reqJson, "visibility", visibility ? visibility : "public");
    if (inReplyToId && inReplyToId[0])
        cJSON_AddStringToObject(reqJson, "in_reply_to_id", inReplyToId);

    reqBody = cJSON_PrintUnformatted(reqJson);
    cJSON_Delete(reqJson);

    if (!reqBody)
        return FALSE;

    snprintf(url, sizeof(url), "%s/api/v1/statuses", apiBaseUrl);
    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    if (FS3EHttp_Post(url, headers, "application/json", reqBody, strlen(reqBody), &resp))
    {
        json = cJSON_Parse((char *)resp.fhr_Body);
        if (json)
        {
            FS3EMastodon_CopyJsonString(json, "id", outStatusId, outStatusIdSize);

            ok = (outStatusId[0] != '\0');

            cJSON_Delete(json);
        }

        FS3EHttp_FreeResponse(&resp);
    }

    cJSON_free(reqBody);

    return ok;
}

BOOL FS3EMastodon_EditStatus(const char *apiBaseUrl, const char *accessToken,
                            const char *statusId, const char *statusText,
                            const char *const *mediaIds, ULONG mediaCount)
{
    char url[300];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *reqJson;
    char *reqBody;
    BOOL ok = FALSE;

    reqJson = cJSON_CreateObject();
    if (!reqJson)
        return FALSE;

    cJSON_AddStringToObject(reqJson, "status", statusText);

    /* Only add the key at all when there's something to preserve -- see
     * the header comment: the server only touches attachments when this
     * key is present, so omitting it for a media-less toot is correct,
     * not just harmless. No "visibility" here -- Mastodon's edit endpoint
     * doesn't accept changing it. */
    if (mediaIds && mediaCount > 0)
    {
        cJSON *arr = cJSON_CreateArray();
        if (arr)
        {
            ULONG i;
            for (i = 0; i < mediaCount; i++)
                if (mediaIds[i] && mediaIds[i][0])
                    cJSON_AddItemToArray(arr, cJSON_CreateString(mediaIds[i]));
            cJSON_AddItemToObject(reqJson, "media_ids", arr);
        }
    }

    reqBody = cJSON_PrintUnformatted(reqJson);
    cJSON_Delete(reqJson);

    if (!reqBody)
        return FALSE;

    snprintf(url, sizeof(url), "%s/api/v1/statuses/%s", apiBaseUrl, statusId);
    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    if (FS3EHttp_Put(url, headers, "application/json", reqBody, strlen(reqBody), &resp))
    {
        ok = (resp.fhr_StatusCode == 200);
        FS3EHttp_FreeResponse(&resp);
    }

    cJSON_free(reqBody);

    return ok;
}

BOOL FS3EMastodon_DeleteStatus(const char *apiBaseUrl, const char *accessToken,
                              const char *statusId)
{
    char url[300];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    BOOL ok = FALSE;

    snprintf(url, sizeof(url), "%s/api/v1/statuses/%s", apiBaseUrl, statusId);
    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    if (FS3EHttp_Delete(url, headers, &resp))
    {
        ok = (resp.fhr_StatusCode == 200);
        FS3EHttp_FreeResponse(&resp);
    }

    return ok;
}

BOOL FS3EMastodon_Favourite(const char *apiBaseUrl, const char *accessToken,
                           const char *statusId, BOOL favourite,
                           BOOL *outFavourited)
{
    char url[300];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    *outFavourited = FALSE;

    snprintf(url, sizeof(url), "%s/api/v1/statuses/%s/%s", apiBaseUrl, statusId,
             favourite ? "favourite" : "unfavourite");
    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    /* Empty body -- Mastodon's favourite/unfavourite endpoints take none,
     * only the auth header and the :id in the URL. */
    if (FS3EHttp_Post(url, headers, "application/json", "", 0, &resp))
    {
        json = cJSON_Parse((char *)resp.fhr_Body);
        if (json)
        {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, "favourited");
            *outFavourited = (v && cJSON_IsTrue(v)) ? TRUE : FALSE;

            ok = TRUE;
            cJSON_Delete(json);
        }

        FS3EHttp_FreeResponse(&resp);
    }

    return ok;
}

BOOL FS3EMastodon_LookupAccount(const char *apiBaseUrl, const char *accessToken,
                                const char *acct, FS3EMastodonAccount *outAccount)
{
    char url[300];
    char encAcct[160];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    FS3EMastodon_UrlEncode(acct, encAcct, sizeof(encAcct));
    snprintf(url, sizeof(url), "%s/api/v1/accounts/lookup?acct=%s", apiBaseUrl, encAcct);

    if (accessToken && accessToken[0]) {
        FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);
        headers[0].fhh_Name  = "Authorization";
        headers[0].fhh_Value = authHeader;
        headers[1].fhh_Name  = NULL;
        headers[1].fhh_Value = NULL;
    } else {
        headers[0].fhh_Name  = NULL;
        headers[0].fhh_Value = NULL;
    }

    if (!FS3EHttp_Get(url, headers, &resp))
        return FALSE;

    json = cJSON_Parse((char *)resp.fhr_Body);
    if (json)
    {
        const cJSON *v;

        outAccount->fma_Id          = FS3EMastodon_DupJsonString(json, "id");
        outAccount->fma_Username    = FS3EMastodon_DupJsonString(json, "username");
        outAccount->fma_Acct        = FS3EMastodon_DupJsonString(json, "acct");
        outAccount->fma_DisplayName = FS3EMastodon_DupJsonString(json, "display_name");
        outAccount->fma_AvatarURL   = FS3EMastodon_DupJsonString(json, "avatar");
        outAccount->fma_Note        = FS3EMastodon_DupJsonString(json, "note"); /* raw HTML, see header comment */

        v = cJSON_GetObjectItemCaseSensitive(json, "followers_count");
        outAccount->fma_FollowersCount = (v && cJSON_IsNumber(v)) ? (ULONG)v->valueint : 0;

        v = cJSON_GetObjectItemCaseSensitive(json, "following_count");
        outAccount->fma_FollowingCount = (v && cJSON_IsNumber(v)) ? (ULONG)v->valueint : 0;

        ok = (outAccount->fma_Id != NULL);

        cJSON_Delete(json);
    }

    FS3EHttp_FreeResponse(&resp);

    return ok;
}

BOOL FS3EMastodon_GetRelationship(const char *apiBaseUrl, const char *accessToken,
                                  const char *accountId, BOOL *outFollowing)
{
    char url[300];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    *outFollowing = FALSE;

    snprintf(url, sizeof(url), "%s/api/v1/accounts/relationships?id[]=%s", apiBaseUrl, accountId);
    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    if (!FS3EHttp_Get(url, headers, &resp))
        return FALSE;

    json = cJSON_Parse((char *)resp.fhr_Body);
    if (json && cJSON_IsArray(json))
    {
        const cJSON *item = cJSON_GetArrayItem(json, 0);
        if (item)
        {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(item, "following");
            *outFollowing = (v && cJSON_IsTrue(v)) ? TRUE : FALSE;
            ok = TRUE;
        }
    }
    if (json) cJSON_Delete(json);

    FS3EHttp_FreeResponse(&resp);

    return ok;
}

/* Batch counterpart of FS3EMastodon_GetRelationship above -- one
 * repeated id[] per account instead of a single id, and the whole parsed
 * Relationship array is handed back via outJson (not pre-extracted) since
 * the caller (FS3ENet_HandleRelationships) needs to match each entry to
 * its own account id, not just read one flag. URL is AllocVec'd rather
 * than a fixed on-stack buffer -- a full page of ids (see
 * FS3ENET_ACCLIST_FOLLOWERS/FOLLOWING's limit=40) can run well past the
 * 256/512-byte stack buffers every other URL builder in this file gets
 * away with. */
BOOL FS3EMastodon_GetRelationships(const char *apiBaseUrl, const char *accessToken,
                                   const char *const *accountIds, ULONG count,
                                   cJSON **outJson)
{
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    char *url;
    ULONG urlCap, i;
    char *w;
    BOOL ok = FALSE;

    *outJson = NULL;
    if (count == 0) return FALSE;

    urlCap = (ULONG)strlen(apiBaseUrl) + 48; /* "/api/v1/accounts/relationships?id[]=" + slack */
    for (i = 0; i < count; i++)
        urlCap += (ULONG)strlen(accountIds[i]) + 8; /* "&id[]=" + id + slack */

    url = (char *)AllocVec(urlCap, MEMF_ANY);
    if (!url) return FALSE;

    w  = url;
    w += snprintf(w, urlCap, "%s/api/v1/accounts/relationships?id[]=%s",
                  apiBaseUrl, accountIds[0]);
    for (i = 1; i < count; i++)
        w += snprintf(w, (ULONG)(url + urlCap - w), "&id[]=%s", accountIds[i]);

    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    if (!FS3EHttp_Get(url, headers, &resp)) {
        FreeVec(url);
        return FALSE;
    }
    FreeVec(url);

    *outJson = cJSON_Parse((char *)resp.fhr_Body);
    ok = (*outJson && cJSON_IsArray(*outJson));
    if (!ok && *outJson) {
        cJSON_Delete(*outJson);
        *outJson = NULL;
    }

    FS3EHttp_FreeResponse(&resp);

    return ok;
}

BOOL FS3EMastodon_Follow(const char *apiBaseUrl, const char *accessToken,
                         const char *accountId, BOOL follow,
                         BOOL *outFollowing)
{
    char url[300];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *json;
    BOOL ok = FALSE;

    *outFollowing = FALSE;

    snprintf(url, sizeof(url), "%s/api/v1/accounts/%s/%s", apiBaseUrl, accountId,
             follow ? "follow" : "unfollow");
    FS3EMastodon_BuildAuthHeader(authHeader, sizeof(authHeader), accessToken);

    headers[0].fhh_Name  = "Authorization";
    headers[0].fhh_Value = authHeader;
    headers[1].fhh_Name  = NULL;
    headers[1].fhh_Value = NULL;

    /* Empty body -- Mastodon's follow/unfollow endpoints take none, only
     * the auth header and the :id in the URL. */
    if (FS3EHttp_Post(url, headers, "application/json", "", 0, &resp))
    {
        json = cJSON_Parse((char *)resp.fhr_Body);
        if (json)
        {
            const cJSON *v = cJSON_GetObjectItemCaseSensitive(json, "following");
            *outFollowing = (v && cJSON_IsTrue(v)) ? TRUE : FALSE;

            ok = TRUE;
            cJSON_Delete(json);
        }

        FS3EHttp_FreeResponse(&resp);
    }

    return ok;
}
