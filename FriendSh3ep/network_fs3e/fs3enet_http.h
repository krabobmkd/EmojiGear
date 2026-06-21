#ifndef FS3ENET_HTTP_H
#define FS3ENET_HTTP_H

/*
 * FriendSh3ep network process - HTTP(S) GET/POST wrappers around AmiSSL v5
 * (OSSL_HTTP_transfer). See ../ARCHITECTURE.md section 4.
 *
 * FS3EHttp_Init() must be called once from the network process before any
 * other function here, and FS3EHttp_Cleanup() once before the process exits.
 */

#include <exec/types.h>

/* One HTTP header as a name/value pair. An array of these, terminated by
 * a {NULL, NULL} entry, can be passed to FS3EHttp_Get()/FS3EHttp_Post(); pass
 * NULL for no extra headers. */
typedef struct FS3EHttpHeader
{
    const char *fhh_Name;
    const char *fhh_Value;
} FS3EHttpHeader;

/* Response body. fhr_Body is AllocVec()'d with length fhr_BodyLen plus a
 * trailing NUL (not counted in fhr_BodyLen), so it can be passed directly
 * to cJSON_Parse(). Free with FS3EHttp_FreeResponse(). */
typedef struct FS3EHttpResponse
{
    APTR  fhr_Body;
    ULONG fhr_BodyLen;
} FS3EHttpResponse;

/* Opens bsdsocket.library + amisslmaster.library and initializes AmiSSL.
 * Returns TRUE on success. */
BOOL FS3EHttp_Init(void);

/* Shuts down AmiSSL and closes the libraries opened by FS3EHttp_Init(). */
void FS3EHttp_Cleanup(void);

/* Blocking HTTPS GET. On success fills *out and returns TRUE; on failure
 * (connection/TLS error, or no response received) returns FALSE and *out
 * is zeroed. */
BOOL FS3EHttp_Get(const char *url, const FS3EHttpHeader *headers,
                FS3EHttpResponse *out);

/* Blocking HTTPS POST of body/bodyLen with the given Content-Type. */
BOOL FS3EHttp_Post(const char *url, const FS3EHttpHeader *headers,
                 const char *contentType,
                 const void *body, ULONG bodyLen,
                 FS3EHttpResponse *out);

/* Frees a response filled in by FS3EHttp_Get()/FS3EHttp_Post(). */
void FS3EHttp_FreeResponse(FS3EHttpResponse *out);

/* Dumps the current OpenSSL error queue to Output(), for diagnostics when
 * FS3EHttp_Get()/FS3EHttp_Post() return FALSE. */
void FS3EHttp_PrintErrors(void);

#endif /* FS3ENET_HTTP_H */
