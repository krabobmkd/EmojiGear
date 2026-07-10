/*
 * FriendSh3ep network process - HTTP(S) GET/POST wrappers.
 *
 * AmiSSL setup mirrors EmojiGear/testsocket/httpget.c: bsdsocket.library +
 * amisslmaster.library are opened manually and AmiSSL is initialized with
 * AmiSSL_ErrNoPtr pointing at libnix' `errno` (USE_AUTOINIT/libamisslauto.a
 * is incompatible with libnix, see network/CMakeLists.txt).
 *
 * GET and POST both go through OSSL_HTTP_transfer(), which is the
 * all-in-one helper covering OSSL_HTTP_get()'s use case (req == NULL) plus
 * POST (req != NULL, with a Content-Type).
 */

#include "fs3enet_http.h"

#include <errno.h>
#include <stdio.h>

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>

#include <amissl/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>

#if !defined(__amigaos4__)
# include <SDI_compiler.h>
#endif

#define FS3EHTTP_USER_AGENT  "AmigaOS3 FriendSh3ep Beta/0.1"
#define FS3EHTTP_INITIAL_BUF 4096
#define FS3EHTTP_MAX_PATH    2048

/* OSSL_HTTP_transfer's `timeout` arg, in seconds -- 0 means "no timeout,
 * block forever", which is the unsafe part: a single dead/stalled
 * connection then freezes the whole single-threaded network process
 * permanently (see FS3ENet_ProcEntry). We tried positive values (30,
 * then 60) to bound that, which instead deterministically truncated
 * every large download at a point that scaled with file size. Traced
 * this to actual OpenSSL source (see amissl_timeout_report/ at the
 * FriendSh3ep project root, which has git-cloned AmiSSL's source and the
 * full writeup): `timeout` is NOT "how long to wait for the connection"
 * -- OSSL_HTTP_transfer() passes it to OSSL_HTTP_open() as the connect-
 * phase deadline, computed ONCE as time(NULL)+timeout BEFORE any data
 * is sent, then reuses that exact same one-shot deadline (rctx->max_time
 * == rctx->max_total_time) for the ENTIRE exchange -- headers AND the
 * full body, however long our own subsequent BIO_read() loop takes to
 * drain it. There is no value of `timeout` here that's safe for an
 * arbitrarily large/slow download; a bigger number only raises the size
 * threshold where this bites, it doesn't remove it. Properly fixing the
 * dead-connection-hang risk without this tradeoff would mean moving to
 * OSSL_HTTP_open()'s own connect-only overall_timeout plus manually
 * driving OSSL_HTTP_REQ_CTX_nbio()/OSSL_HTTP_exchange() ourselves,
 * never re-arming an exchange-wide deadline -- a real architecture
 * change, not done here. For now, 0 is the *correct* choice given we
 * don't bound download size/duration, not just a fallback. */
#define FS3EHTTP_TIMEOUT_SECS 0

/* OSSL_HTTP_transfer's `max_resp_len` arg, in bytes -- confirmed from
 * source (check_max_len(), http_client.c) that 0 really does mean "no
 * limit" for this call chain: `if (max_len != 0 && len > max_len)` is
 * skipped entirely when max_len==0. (A different entry point,
 * OSSL_HTTP_REQ_CTX_set_max_response_length(), substitutes a default
 * when passed 0 -- that's a different function, not what we call here.)
 * So this was never the cause of the truncation above -- set explicitly
 * anyway as good hygiene, so nothing we fetch can silently depend on
 * which of those two behaviors a given code path happens to have. */
#define FS3EHTTP_MAX_RESP_LEN (32UL * 1024UL * 1024UL) /* 32 MiB */

struct Library *AmiSSLMasterBase=NULL, *SocketBase=NULL;
struct Library *AmiSSLBase=NULL, *AmiSSLExtBase=NULL;

static SSL_CTX *g_SSLCtx=NULL;

/* Required callback to enable HTTPS connections via OSSL_HTTP_transfer(). */
static SAVEDS STDARGS BIO *FS3EHttp_TLSCallback(BIO *bio, void *arg, int connect, int detail)
{
    if (connect && detail)
    {
        BIO *sbio = BIO_new_ssl((SSL_CTX *)arg, 1);
        bio = (sbio != NULL) ? BIO_push(sbio, bio) : NULL;
    }

    return bio;
}

/* Stub used when freeing our custom header stack (see httpget.c). */
static SAVEDS STDARGS void FS3EHttp_FreeConfValue(CONF_VALUE *val)
{
    X509V3_conf_free(val);
}

BOOL FS3EHttp_Init(void)
{
    if (!(SocketBase = OpenLibrary("bsdsocket.library", 4)))
        return FALSE;

    if (!(AmiSSLMasterBase = OpenLibrary("amisslmaster.library", AMISSLMASTER_MIN_VERSION)))
    {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return FALSE;
    }

    if (OpenAmiSSLTags(AMISSL_CURRENT_VERSION,
            AmiSSL_UsesOpenSSLStructs, FALSE,
            AmiSSL_GetAmiSSLBase, &AmiSSLBase,
            AmiSSL_GetAmiSSLExtBase, &AmiSSLExtBase,
            AmiSSL_SocketBase, SocketBase,
            AmiSSL_ErrNoPtr, &errno,
            TAG_DONE) != 0)
    {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return FALSE;
    }

    g_SSLCtx = SSL_CTX_new(TLS_client_method());
    if (!g_SSLCtx)
    {
        CloseAmiSSL();
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
        CloseLibrary(SocketBase);
        SocketBase = NULL;
        return FALSE;
    }

    return TRUE;
}

void FS3EHttp_Cleanup(void)
{
    if (g_SSLCtx)
    {
        SSL_CTX_free(g_SSLCtx);
        g_SSLCtx = NULL;
        CloseAmiSSL();
    }

    if (AmiSSLMasterBase)
    {
        CloseLibrary(AmiSSLMasterBase);
        AmiSSLMasterBase = NULL;
    }

    if (SocketBase)
    {
        CloseLibrary(SocketBase);
        SocketBase = NULL;
    }
}

void FS3EHttp_PrintErrors(void)
{
    BIO *bio_err;

    /* Defensive: never touch AmiSSL if FS3EHttp_Init() never succeeded
     * (or already ran FS3EHttp_Cleanup()) -- AmiSSLExtBase is only ever
     * non-NULL between a successful OpenAmiSSLTags() and the matching
     * CloseAmiSSL(). */
    if (!AmiSSLExtBase)
        return;

    bio_err = BIO_new(BIO_s_file());

    if (bio_err)
    {
        BIO_set_fp_amiga(bio_err, Output(), BIO_NOCLOSE | BIO_FP_TEXT);
        ERR_print_errors(bio_err);
        BIO_free(bio_err);
    }
}

/* Reads all of bio into an AllocVec()'d, NUL-terminated buffer. */
static BOOL FS3EHttp_ReadBody(BIO *bio, FS3EHttpResponse *out)
{
    ULONG  cap = FS3EHTTP_INITIAL_BUF;
    ULONG  len = 0;
    UBYTE *buf = AllocVec(cap, MEMF_ANY);

    if (!buf)
        return FALSE;

    for (;;)
    {
        int n;

        if (len + 4096 + 1 > cap)
        {
            ULONG  newcap = cap * 2;
            UBYTE *newbuf = AllocVec(newcap, MEMF_ANY);

            if (!newbuf)
            {
                FreeVec(buf);
                return FALSE;
            }

            CopyMem(buf, newbuf, len);
            FreeVec(buf);
            buf = newbuf;
            cap = newcap;
        }

        n = BIO_read(bio, buf + len, 4096);
        if (n <= 0)
        {
            /* Restored to the original, known-working behavior: treat
             * any n<=0 as "done". Two attempts at distinguishing a real
             * read error from clean EOF (BIO_eof()+error queue, then
             * n<0-only) both misfired on this BIO chain -- the second
             * one specifically broke large downloads, which apparently
             * hit an n<0 return mid-transfer on perfectly healthy
             * connections (see amissl_timeout_report/BUG_REPORT.md at
             * the FriendSh3ep project root for the full writeup and a
             * standalone repro case). Silently truncating on a real
             * timeout is a known, documented tradeoff for now -- better
             * than failing every large download outright, until AmiSSL's
             * actual timeout semantics for OSSL_HTTP_transfer's returned
             * BIO are understood. */
            break;
        }

        len += (ULONG)n;
    }

    buf[len] = '\0';

    out->fhr_Body    = buf;
    out->fhr_BodyLen = len;

    return TRUE;
}

/* Shared GET/POST implementation. reqBody/contentType are NULL for GET. */
static BOOL FS3EHttp_DoRequest(const char *url, const FS3EHttpHeader *extraHeaders,
                              const char *contentType, BIO *reqBody,
                              FS3EHttpResponse *out)
{
    int    useSSL, portNum;
    char  *user = NULL, *host = NULL, *port = NULL;
    char  *path = NULL, *query = NULL, *frag = NULL;
    char   pathBuf[FS3EHTTP_MAX_PATH];
    char   portBuf[16];
    STACK_OF(CONF_VALUE) *headers = NULL;
    OSSL_HTTP_REQ_CTX *rctx = NULL;
    BIO   *resp;
    BOOL   ok = FALSE;

    out->fhr_Body    = NULL;
    out->fhr_BodyLen = 0;

    /* Defensive: never call into AmiSSL/OpenSSL if FS3EHttp_Init() never
     * succeeded. Structurally this can't happen today -- FS3ENet_ProcEntry
     * only enters its dispatch loop (the only caller of FS3EHttp_Get/Post)
     * when FS3EHttp_Init() returned TRUE -- but this keeps the invariant
     * true at the point of use too, not just at the call site. */
    if (!AmiSSLExtBase)
        return FALSE;

    if (!OSSL_HTTP_parse_url(url, &useSSL, &user, &host, &port, &portNum,
                             &path, &query, &frag))
        return FALSE;

    if (!port)
    {
        snprintf(portBuf, sizeof(portBuf), "%d", portNum);
        port = portBuf;
    }

    if (query && query[0])
        snprintf(pathBuf, sizeof(pathBuf), "%s?%s", path ? path : "/", query);
    else
        snprintf(pathBuf, sizeof(pathBuf), "%s", path ? path : "/");

    X509V3_add_value("User-Agent", FS3EHTTP_USER_AGENT, &headers);

    if (extraHeaders)
    {
        const FS3EHttpHeader *h;

        for (h = extraHeaders; h->fhh_Name; h++)
            X509V3_add_value(h->fhh_Name, h->fhh_Value, &headers);
    }

    resp = OSSL_HTTP_transfer(&rctx, host, port, pathBuf, useSSL,
                               NULL, NULL,
                               NULL, NULL,
                               FS3EHttp_TLSCallback, g_SSLCtx,
                               0, headers,
                               contentType, reqBody,
                               NULL, 0,
                               FS3EHTTP_MAX_RESP_LEN, FS3EHTTP_TIMEOUT_SECS, 0);

    if (resp)
    {
        ok = FS3EHttp_ReadBody(resp, out);
        BIO_free(resp);
    }
    else
    {
        /* OSSL_HTTP_transfer() gives no reason code -- dump the OpenSSL/
         * AmiSSL error queue (DNS failure, connection refused, TLS
         * handshake failure, etc.) so a bare "GET failed" printf isn't the
         * only clue when this happens. */
        printf("net: HTTP transfer failed for %s://%s:%s%s\n",
               useSSL ? "https" : "http", host ? host : "?", port, pathBuf);
        FS3EHttp_PrintErrors();
    }

    sk_CONF_VALUE_pop_free(headers, FS3EHttp_FreeConfValue);

    if (user)
        OPENSSL_free(user);
    if (host)
        OPENSSL_free(host);
    if (port != portBuf)
        OPENSSL_free(port);
    if (path)
        OPENSSL_free(path);
    if (query)
        OPENSSL_free(query);
    if (frag)
        OPENSSL_free(frag);

    return ok;
}

BOOL FS3EHttp_Get(const char *url, const FS3EHttpHeader *headers, FS3EHttpResponse *out)
{
    return FS3EHttp_DoRequest(url, headers, NULL, NULL, out);
}

BOOL FS3EHttp_Post(const char *url, const FS3EHttpHeader *headers,
                 const char *contentType,
                 const void *body, ULONG bodyLen,
                 FS3EHttpResponse *out)
{
    BIO  *reqBody = BIO_new_mem_buf(body, (int)bodyLen);
    BOOL  ok;

    if (!reqBody)
        return FALSE;

    ok = FS3EHttp_DoRequest(url, headers, contentType, reqBody, out);

    BIO_free(reqBody);

    return ok;
}

void FS3EHttp_FreeResponse(FS3EHttpResponse *out)
{
    if (out->fhr_Body)
        FreeVec(out->fhr_Body);

    out->fhr_Body    = NULL;
    out->fhr_BodyLen = 0;
}
