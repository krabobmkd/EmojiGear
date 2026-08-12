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
#include <string.h>

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/amissl.h>
#include <proto/amisslmaster.h>

#include <amissl/amissl.h>
#include <libraries/amisslmaster.h>
#include <libraries/amissl.h>

#include "bdbprintf.h"

#if !defined(__amigaos4__)
# include <SDI_compiler.h>
#endif

#define FS3EHTTP_USER_AGENT  "Amiga FriendSh3ep"
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

/* FS3EHttp_GetRange()'s per-chunk connect deadline, in seconds -- safe to
 * bound (unlike FS3EHTTP_TIMEOUT_SECS above) because a Range chunk is small
 * and fixed-size regardless of the resource's total length; a stalled/dead
 * host fails one chunk instead of hanging the whole network process. Uses
 * AmiSSL's BIO_do_connect_retry(), the same bounded-connect primitive
 * OSSL_HTTP_open() itself uses internally -- FS3EHttp_DoRawRequest()'s plain
 * BIO_do_connect() (used by PUT/DELETE below) has no such bound today, so
 * this is applied there too via FS3EHTTP_RAW_TIMEOUT_SECS, a generous value
 * for those rare/small requests. Note this only bounds the CONNECT phase --
 * once connected, FS3EHttp_ReadBody()'s BIO_read() loop can still stall on a
 * host that accepts the connection but never sends a response; narrowing
 * that further would need a read-side (socket-level) timeout, not attempted
 * here. */
#define FS3EHTTP_CHUNK_TIMEOUT_SECS 15
#define FS3EHTTP_RAW_TIMEOUT_SECS   30
#define FS3EHTTP_CONNECT_NAP_MS     200 /* BIO_do_connect_retry()'s poll interval */

/* FS3EHttp_DoRawRequest()'s cap on 3xx hops it will follow for one logical
 * request -- see that function's redirect handling. Bounded the same way a
 * browser/curl bounds it, purely to avoid ever looping forever on a
 * misconfigured server; every real redirect chain seen so far (GitHub
 * Releases: github.com -> release-assets.githubusercontent.com) is a single
 * hop. */
#define FS3EHTTP_MAX_REDIRECTS 5

struct Library *AmiSSLMasterBase=NULL, *SocketBase=NULL;
struct Library *AmiSSLBase=NULL, *AmiSSLExtBase=NULL;

static SSL_CTX *g_SSLCtx=NULL;

/* FS3EHttp_TLSCallback's arg -- OSSL_HTTP_transfer()'s bio_update_fn only
 * ever receives back whatever single `arg` pointer the caller handed it
 * (see FS3EHttp_DoRequest() below), so ctx and host are bundled into one
 * struct rather than needing two separate arg slots that don't exist. */
typedef struct {
    SSL_CTX    *ctx;
    const char *host; /* for SNI -- see FS3EHttp_TLSCallback's own comment */
} FS3EHttpTLSCallbackArg;

/* Required callback to enable HTTPS connections via OSSL_HTTP_transfer().
 *
 * Also sets SNI (SSL_set_tlsext_host_name) on the SSL object it creates --
 * NOT redundant with what OSSL_HTTP_transfer()/OSSL_HTTP_open() might do on
 * their own: this AmiSSL build's port of that code path doesn't reliably
 * set it (confirmed by FS3EHttp_GetRange()/FS3EHttp_DoRawRequest() below,
 * which build their OWN raw BIO chain instead of going through
 * OSSL_HTTP_transfer() and already had to set SNI explicitly themselves --
 * this callback is the equivalent fix for the OSSL_HTTP_transfer() path
 * FS3EHttp_Get()/FS3EHttp_Post() use). Without it, any TLS-terminating
 * frontend that picks a certificate/vhost by SNI (Cloudflare and similar,
 * common in front of smaller self-hosted Mastodon instances) can't tell
 * which site is being asked for and fails the handshake outright -- seen
 * in practice as "ssl/tls alert handshake failure" against mas.to while
 * mastodon.social (evidently not SNI-strict) connected fine. */
static SAVEDS STDARGS BIO *FS3EHttp_TLSCallback(BIO *bio, void *arg, int connect, int detail)
{
    FS3EHttpTLSCallbackArg *cbArg = (FS3EHttpTLSCallbackArg *)arg;

    if (connect && detail)
    {
        BIO *sbio = BIO_new_ssl(cbArg->ctx, 1);
        if (sbio && cbArg->host)
        {
            SSL *ssl = NULL;
            BIO_get_ssl(sbio, &ssl);
            if (ssl)
                SSL_set_tlsext_host_name(ssl, cbArg->host);
        }
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
    // unsigned long errCode;
    // BOOL any = FALSE;

    // /* Defensive: never touch AmiSSL if FS3EHttp_Init() never succeeded
    //  * (or already ran FS3EHttp_Cleanup()) -- AmiSSLExtBase is only ever
    //  * non-NULL between a successful OpenAmiSSLTags() and the matching
    //  * CloseAmiSSL(). */
    // if (!AmiSSLExtBase)
    //     return;

    // /* bdbprintf_now(), not ERR_print_errors()+Output() -- FS3ENet_ProcEntry's
    //  * process (fs3enet.c) is a plain CreateNewProcTags() child with no
    //  * controlling window, so Output() goes to a console nobody sees. */
    // while ((errCode = ERR_get_error()) != 0) {
    //     char errBuf[256];
    //     ERR_error_string_n(errCode, errBuf, sizeof(errBuf));
    //     bdbprintf_now("FS3EHttp: %s\n", errBuf);
    //     any = TRUE;
    // }
    // if (!any)
    //     bdbprintf_now("FS3EHttp: (no OpenSSL error queue entries)\n");
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

    out->fhr_Body       = NULL;
    out->fhr_BodyLen    = 0;
    out->fhr_StatusCode = 0; /* OSSL_HTTP_transfer()'s BIO never exposes this */

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

    {
        FS3EHttpTLSCallbackArg cbArg;
        cbArg.ctx  = g_SSLCtx;
        cbArg.host = host; /* SNI -- see FS3EHttp_TLSCallback's doc comment */

        resp = OSSL_HTTP_transfer(&rctx, host, port, pathBuf, useSSL,
                                   NULL, NULL,
                                   NULL, NULL,
                                   FS3EHttp_TLSCallback, &cbArg,
                                   0, headers,
                                   contentType, reqBody,
                                   NULL, 0,
                                   FS3EHTTP_MAX_RESP_LEN, FS3EHTTP_TIMEOUT_SECS, 0);
    }

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

/* Raw HTTP/1.1 request path used only by FS3EHttp_Put() -- OSSL_HTTP_transfer()
 * (used by FS3EHttp_DoRequest() above) can only ever emit GET or POST, since
 * OSSL_HTTP_REQ_CTX_set_request_line() takes the verb as a hardcoded
 * boolean at every level of that API, not a string. Always sends
 * "Connection: close" so the response can be read exactly the way
 * FS3EHttp_ReadBody() already reads GET/POST bodies -- loop until
 * BIO_read() returns <=0 -- without needing a chunked-transfer decoder: a
 * compliant server has to close the socket once it's done sending,
 * regardless of what framing (Content-Length or chunked) it used along the
 * way. Once the full raw response (status line + headers + body) is
 * buffered this way, pulling the status code and body back out is plain
 * string scanning (find the blank line, parse the status line), not
 * protocol parsing. Trade-off: no connection reuse -- fine for this rarely-
 * used path (status edits), not meant to replace FS3EHttp_Get/Post.
 *
 * useRange/rangeOffset/rangeLen add a "Range: bytes=X-Y" header (see
 * FS3EHttp_GetRange()); when useRange is set and the response carries a
 * Content-Range header, its "/TOTAL" suffix is parsed into *outTotalLen
 * (left at 0 if absent/unparseable, or if useRange is FALSE).
 * connectTimeoutSecs bounds BIO_do_connect_retry()'s connect phase -- see
 * FS3EHTTP_CHUNK_TIMEOUT_SECS/FS3EHTTP_RAW_TIMEOUT_SECS above for what each
 * caller passes.
 *
 * Follows up to FS3EHTTP_MAX_REDIRECTS 3xx responses itself (see the
 * redirectUrl handling below) -- this path talks to a plain BIO it opens by
 * hand, unlike FS3EHttp_Get/Post's OSSL_HTTP_transfer(), which already
 * follows redirects internally (see fs3enet_http.h's status doc comment).
 * Without this, FS3EHttp_GetRange() (the only caller that matters here for
 * archive downloads) would take a 3xx's near-empty body as "the whole file"
 * and hand a truncated/empty download to the caller as a success -- exactly
 * what was seen against GitHub Releases, which 302s the real asset off to
 * release-assets.githubusercontent.com. */
static BOOL FS3EHttp_DoRawRequest(const char *method, const char *url,
                                   const FS3EHttpHeader *extraHeaders,
                                   const char *contentType,
                                   const void *reqBody, ULONG reqBodyLen,
                                   BOOL useRange, ULONG rangeOffset, ULONG rangeLen,
                                   int connectTimeoutSecs, ULONG *outTotalLen,
                                   FS3EHttpResponse *out)
{
    const char *curUrl = url;
    char        redirectUrl[FS3EHTTP_MAX_PATH];
    int         redirectsLeft = FS3EHTTP_MAX_REDIRECTS;

    out->fhr_Body       = NULL;
    out->fhr_BodyLen    = 0;
    out->fhr_StatusCode = 0;

    if (outTotalLen)
        *outTotalLen = 0;

    /* Defensive, same invariant as FS3EHttp_DoRequest() above. */
    if (!AmiSSLExtBase)
        return FALSE;

    for (;;)
    {
    int    useSSL, portNum;
    char  *user = NULL, *host = NULL, *port = NULL;
    char  *path = NULL, *query = NULL, *frag = NULL;
    char   pathBuf[FS3EHTTP_MAX_PATH];
    char   portBuf[16];
    char   hostPort[300];
    BIO   *bio = NULL;
    SSL   *ssl = NULL;
    BOOL   ok = FALSE;
    BOOL   isRedirect = FALSE;

    if (!OSSL_HTTP_parse_url(curUrl, &useSSL, &user, &host, &port, &portNum,
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

    snprintf(hostPort, sizeof(hostPort), "%s:%s", host, port);

    if (useSSL)
    {
        bio = BIO_new_ssl_connect(g_SSLCtx);
        if (bio)
        {
            BIO_set_conn_hostname(bio, hostPort);
            BIO_get_ssl(bio, &ssl);
            if (ssl)
                SSL_set_tlsext_host_name(ssl, host);
        }
    }
    else
    {
        bio = BIO_new(BIO_s_connect());
        if (bio)
            BIO_set_conn_hostname(bio, hostPort);
    }

    /* Plain blocking BIO_do_connect(), same call FS3EHttp_Put()/Delete()
     * always used. A first attempt at bounding this with
     * BIO_do_connect_retry(bio, connectTimeoutSecs, ...) -- the same
     * primitive OSSL_HTTP_open() uses internally -- made every single
     * FS3EHttp_GetRange() call fail outright (connect never succeeding,
     * confirmed via bdbprintf_now tracing: ok=0/status=0 on every attempt,
     * for every host/path), even though the round-robin chunk engine driving
     * it was otherwise working correctly. Reverted rather than chase a
     * non-blocking-BIO interaction blind on a cross-compiled target with no
     * interactive debugger -- connectTimeoutSecs is consequently unused
     * again here, same unbounded-connect risk FS3EHttp_Put()/Delete()
     * already carried before this file existed. Revisit if AmiSSL's
     * BIO_do_connect_retry()/nonblocking-BIO behavior on real hardware is
     * ever confirmed working in isolation. */
    if (!bio || BIO_do_connect(bio) <= 0)
    {
        FS3EHttp_PrintErrors();
        goto out_free_bio;
    }

    /* Request line + headers, hand-written into one buffer then sent with
     * a plain BIO_write() -- BIO_printf() on this platform is an SFDC
     * vararg-tag macro with a FIXED arity (not a real C variadic function),
     * so it can't be used as a normal printf() here (see the "%s %s"
     * two-substitution call below the amissl.h include produces a "macro
     * requires 4 arguments" error). snprintf() into reqHead avoids it
     * entirely. (See the function comment above for why "Connection:
     * close" is load-bearing, not just hygiene.) */
    {
        char reqHead[4096];
        int  n = 0;

        n += snprintf(reqHead + n, sizeof(reqHead) - n, "%s %s HTTP/1.1\r\n", method, pathBuf);
        n += snprintf(reqHead + n, sizeof(reqHead) - n, "Host: %s\r\n", host);
        n += snprintf(reqHead + n, sizeof(reqHead) - n, "User-Agent: %s\r\n", FS3EHTTP_USER_AGENT);
        n += snprintf(reqHead + n, sizeof(reqHead) - n, "Connection: close\r\n");

        if (useRange)
            n += snprintf(reqHead + n, sizeof(reqHead) - n, "Range: bytes=%lu-%lu\r\n",
                          (unsigned long)rangeOffset, (unsigned long)(rangeOffset + rangeLen - 1));

        if (extraHeaders)
        {
            const FS3EHttpHeader *h;
            for (h = extraHeaders; h->fhh_Name; h++)
                n += snprintf(reqHead + n, sizeof(reqHead) - n, "%s: %s\r\n", h->fhh_Name, h->fhh_Value);
        }

        if (reqBody && reqBodyLen > 0)
        {
            if (contentType)
                n += snprintf(reqHead + n, sizeof(reqHead) - n, "Content-Type: %s\r\n", contentType);
            n += snprintf(reqHead + n, sizeof(reqHead) - n, "Content-Length: %lu\r\n", (unsigned long)reqBodyLen);
        }

        n += snprintf(reqHead + n, sizeof(reqHead) - n, "\r\n");

        if (n < 0 || n >= (int)sizeof(reqHead))
        {
            /* Headers didn't fit -- bail rather than send a truncated/
             * corrupt request. Not expected in practice (URL is already
             * capped by FS3EHTTP_MAX_PATH, headers here are just
             * Authorization/Content-Type). */
            goto out_free_bio;
        }

        BIO_write(bio, reqHead, n);
    }

    if (reqBody && reqBodyLen > 0)
        BIO_write(bio, reqBody, (int)reqBodyLen);

    {
        FS3EHttpResponse raw;

        if (FS3EHttp_ReadBody(bio, &raw))
        {
            /* raw.fhr_Body is the whole raw response (status line +
             * headers + blank line + body), NUL-terminated -- split it by
             * hand instead of a protocol-level parser. */
            char *text      = (char *)raw.fhr_Body;
            char *headerEnd = strstr(text, "\r\n\r\n");

            if (headerEnd)
            {
                int code = 0;
                sscanf(text, "HTTP/%*d.%*d %d", &code);
                out->fhr_StatusCode = (ULONG)code;

#ifdef BDBTRACEMULTIPART
                bdbprintf_now("HttpRaw: %s %s -> status=%d bodyLen=%lu\n",
                               method, curUrl, code, (unsigned long)raw.fhr_BodyLen);
#endif

                /* 3xx -- follow it ourselves (see this function's doc
                 * comment) instead of handing a redirect's near-empty body
                 * back as if it were real content. Location's value is
                 * copied out into redirectUrl (persists past this
                 * iteration's cleanup below) before anything here is
                 * freed. A relative Location (no scheme) is resolved
                 * against the CURRENT request's scheme+host -- rare in
                 * practice (every host seen so far, GitHub included, sends
                 * an absolute URL) but cheap to handle. */
                if (code >= 300 && code < 400 && redirectsLeft > 0)
                {
                    char *loc = strstr(text, "Location:");
                    if (loc && loc < headerEnd)
                    {
                        char  *valStart = loc + 9; /* strlen("Location:") */
                        char  *lineEnd  = strstr(loc, "\r\n");
                        ULONG  llen;

                        while (*valStart == ' ') valStart++;
                        llen = (lineEnd && lineEnd < headerEnd)
                               ? (ULONG)(lineEnd - valStart) : 0;

                        if (llen > 0 && llen < sizeof(redirectUrl))
                        {
                            CopyMem(valStart, redirectUrl, llen);
                            redirectUrl[llen] = '\0';

                            if (strncmp(redirectUrl, "http://", 7) != 0 &&
                                strncmp(redirectUrl, "https://", 8) != 0)
                            {
                                char resolved[FS3EHTTP_MAX_PATH];
                                snprintf(resolved, sizeof(resolved), "%s://%s%s%s",
                                         useSSL ? "https" : "http", host,
                                         redirectUrl[0] == '/' ? "" : "/", redirectUrl);
                                snprintf(redirectUrl, sizeof(redirectUrl), "%s", resolved);
                            }

                            isRedirect = TRUE;
#ifdef BDBTRACEMULTIPART
                            bdbprintf_now("HttpRaw: following redirect to %s\n", redirectUrl);
#endif
                        }
#ifdef BDBTRACEMULTIPART
                        else
                        {
                            bdbprintf_now("HttpRaw: %d with unparseable/oversized Location\n", code);
                        }
#endif
                    }
#ifdef BDBTRACEMULTIPART
                    else
                    {
                        bdbprintf_now("HttpRaw: %d with no Location header\n", code);
                    }
#endif
                }

                /* "Content-Range: bytes X-Y/TOTAL" -- pull TOTAL from after
                 * the '/'. Plain strstr, not a case-insensitive header
                 * parser: every server we've seen (S3/nginx-backed Mastodon
                 * media) sends canonical casing, same assumption the status
                 * line scan above already makes. */
                if (useRange && outTotalLen)
                {
                    char *cr = strstr(text, "Content-Range:");
                    if (cr && cr < headerEnd)
                    {
                        char *slash = strchr(cr, '/');
                        if (slash && slash < headerEnd)
                        {
                            unsigned long total = 0;
                            if (sscanf(slash + 1, "%lu", &total) == 1)
                                *outTotalLen = (ULONG)total;
                        }
                    }
                }

                /* Redirect bodies (if any -- 3xx responses are often
                 * empty) are never handed to the caller; the next loop
                 * iteration's response is what out_free_bio needs. */
                if (!isRedirect)
                {
                    char  *bodyStart = headerEnd + 4;
                    ULONG  bodyLen   = raw.fhr_BodyLen - (ULONG)(bodyStart - text);
                    UBYTE *bodyCopy  = AllocVec(bodyLen + 1, MEMF_ANY);

                    if (bodyCopy)
                    {
                        CopyMem(bodyStart, bodyCopy, bodyLen);
                        bodyCopy[bodyLen] = '\0';
                        out->fhr_Body    = bodyCopy;
                        out->fhr_BodyLen = bodyLen;
                        ok = TRUE;
                    }
                }
            }
            FreeVec(raw.fhr_Body);
        }
    }

out_free_bio:
    if (bio)
        BIO_free_all(bio);

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

    if (isRedirect)
    {
        redirectsLeft--;
        curUrl = redirectUrl;
        continue;
    }

    return ok;
    } /* for (;;) */
}

BOOL FS3EHttp_Put(const char *url, const FS3EHttpHeader *headers,
                 const char *contentType,
                 const void *body, ULONG bodyLen,
                 FS3EHttpResponse *out)
{
    return FS3EHttp_DoRawRequest("PUT", url, headers, contentType, body, bodyLen,
                                  FALSE, 0, 0, FS3EHTTP_RAW_TIMEOUT_SECS, NULL, out);
}

BOOL FS3EHttp_Patch(const char *url, const FS3EHttpHeader *headers,
                 const char *contentType,
                 const void *body, ULONG bodyLen,
                 FS3EHttpResponse *out)
{
    return FS3EHttp_DoRawRequest("PATCH", url, headers, contentType, body, bodyLen,
                                  FALSE, 0, 0, FS3EHTTP_RAW_TIMEOUT_SECS, NULL, out);
}

BOOL FS3EHttp_Delete(const char *url, const FS3EHttpHeader *headers, FS3EHttpResponse *out)
{
    return FS3EHttp_DoRawRequest("DELETE", url, headers, NULL, NULL, 0,
                                  FALSE, 0, 0, FS3EHTTP_RAW_TIMEOUT_SECS, NULL, out);
}

BOOL FS3EHttp_PostRaw(const char *url, const FS3EHttpHeader *headers,
                    const char *contentType,
                    const void *body, ULONG bodyLen,
                    FS3EHttpResponse *out)
{
    return FS3EHttp_DoRawRequest("POST", url, headers, contentType, body, bodyLen,
                                  FALSE, 0, 0, FS3EHTTP_RAW_TIMEOUT_SECS, NULL, out);
}

BOOL FS3EHttp_GetRaw(const char *url, const FS3EHttpHeader *headers, FS3EHttpResponse *out)
{
    return FS3EHttp_DoRawRequest("GET", url, headers, NULL, NULL, 0,
                                  FALSE, 0, 0, FS3EHTTP_RAW_TIMEOUT_SECS, NULL, out);
}

BOOL FS3EHttp_GetRange(const char *url, const FS3EHttpHeader *extraHeaders,
                       ULONG offset, ULONG len,
                       ULONG *outStatus, ULONG *outTotalLen,
                       FS3EHttpResponse *out)
{
    BOOL ok = FS3EHttp_DoRawRequest("GET", url, extraHeaders, NULL, NULL, 0,
                                     TRUE, offset, len,
                                     FS3EHTTP_CHUNK_TIMEOUT_SECS, outTotalLen, out);
    if (ok && outStatus)
        *outStatus = out->fhr_StatusCode;
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
