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
 * pass through, space becomes '+', everything else is %XX. */
static void FS3EMastodon_UrlEncode(const char *src, char *dst, ULONG dstSize)
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
 printf("url:%s\n",url);
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

BOOL FS3EMastodon_GetTimeline(const char *apiBaseUrl, const char *accessToken,
                             const char *timeline, cJSON **outJson)
{
    char url[256];
    char authHeader[300];
    FS3EHttpHeader headers[2];
    FS3EHttpResponse resp;
    cJSON *json;

    *outJson = NULL;

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

    printf("net: GetTimeline GET %s\n", url);

    if (!FS3EHttp_Get(url, headers, &resp)) {
        printf("net: GetTimeline HTTP GET failed\n");
        return FALSE;
    }

    printf("net: GetTimeline response %lu bytes\n", resp.fhr_BodyLen);

    json = cJSON_Parse((char *)resp.fhr_Body);

    if (!json || !cJSON_IsArray(json))
    {
        if (!json) {
            const char *errptr = cJSON_GetErrorPtr();
            printf("net: GetTimeline cJSON_Parse failed near: %.80s\n",
                   errptr ? errptr : "(null)");
        } else {
            printf("net: GetTimeline parsed ok but not an array (type=%d)\n",
                   json->type);
            cJSON_Delete(json);
            json = NULL;
        }
        /* Print first 200 bytes of the body for context */
        if (resp.fhr_Body) {
            char preview[201];
            ULONG plen = resp.fhr_BodyLen < 200 ? resp.fhr_BodyLen : 200;
            CopyMem(resp.fhr_Body, preview, plen);
            preview[plen] = '\0';
            printf("net: GetTimeline body start: %s\n", preview);
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
