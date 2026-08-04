/*
 * fs3eaccounts.c - multi-account list persistence and the active-account
 * state, split out of friendsh3ep.c. See fs3eaccounts.h for the public
 * entry points and friendsh3ep.h for struct App/FS3EAccount.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/gadgetclass.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <dos/dos.h>

#include "compilers.h"
#include "bdbprintf.h"

#include "friendsh3ep.h"
#include "fs3eboopsimainwindow.h"
#include "fs3emachineid.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"
#include "avatarimages.h"

#include "TitleBarLayout/fs3etitlebar.h"
#include "TootTimeline/fs3etoottimeline.h"

#include "network_fs3e/fs3enet.h"

#include "fs3erequests.h"
#include "fs3eaccounts.h"
#include "fs3enetworkhelper.h"

void wait2sec();

/* Free stored account credentials.
 * Not static: friendsh3ep.c's exitclose() calls this too -- see the
 * extern declaration there. */
void FS3EApp_FreeAccount(void)
{
    if (app->accountApiBaseUrl)  { FreeVec(app->accountApiBaseUrl);  app->accountApiBaseUrl  = NULL; }
    if (app->accountAccessToken) { FreeVec(app->accountAccessToken); app->accountAccessToken = NULL; }
    if (app->accountDisplayName) { FreeVec(app->accountDisplayName); app->accountDisplayName = NULL; }
    if (app->accountAcct)        { FreeVec(app->accountAcct);        app->accountAcct        = NULL; }
    if (app->accountAvatarURL)   { FreeVec(app->accountAvatarURL);   app->accountAvatarURL   = NULL; }
    if (app->accountId)          { FreeVec(app->accountId);          app->accountId          = NULL; }
}

/* Store account credentials from a LOGIN_FINISH reply.
 * Not static: fs3erequests.c's FS3EApp_HandleNetReply calls this too --
 * see the extern declaration there. */
void FS3EApp_SetAccount(const char *apiBaseUrl, const char *accessToken,
                               const char *displayName, const char *acct,
                               const char *avatarURL, const char *accountId)
{
    /* Only a genuine account change bumps accountGeneration -- a same-
     * account re-confirm (e.g. FS3ENETQ_VERIFY_ACCOUNT's backfill, which
     * calls this with the same apiBaseUrl+acct just to refresh
     * displayName/avatarURL/accountId) must NOT invalidate a TIMELINE
     * request already in flight for this very account, or its reply would
     * be wrongly discarded as stale and leave timelineFetchedMask's bit
     * permanently stuck set with no content ever applied. */
    BOOL sameAccount = app->accountApiBaseUrl && app->accountAcct &&
                        apiBaseUrl && acct &&
                        !strcmp(app->accountApiBaseUrl, apiBaseUrl) &&
                        !strcmp(app->accountAcct, acct);

    FS3EApp_FreeAccount();
    app->accountApiBaseUrl  = NetStrDup(apiBaseUrl);
    app->accountAccessToken = NetStrDup(accessToken);
    app->accountDisplayName = NetStrDup(displayName);
    app->accountAcct        = NetStrDup(acct);
    app->accountAvatarURL   = NetStrDup(avatarURL);
    app->accountId          = NetStrDup(accountId);

    /* Toot button has nothing to post to without a connected account --
     * safe to call before the toot window exists yet (checked inside). */
    FS3ETootView_UpdateSendEnabled(&app->tootView);

    /* The active account just changed -- any FS3ENETQ_TIMELINE request
     * still in flight from before this point now belongs to a stale
     * generation; see accountGeneration's comment in friendsh3ep.h. */
    if (!sameAccount) {
        app->accountGeneration++;

        /* A different server may have a different per-toot character
         * limit (or none confirmed at all yet) -- the old value belongs
         * to the account being left, so it must not keep showing as if
         * confirmed for this one. Refresh once per real account change
         * (an instance's limit essentially never changes mid-session, so
         * there's no reason to re-ask on every same-account re-confirm).
         * FS3ETootView_UpdateCharCount reflects the reset immediately
         * ("Max: -") rather than lagging until the async reply arrives. */
        app->accountMaxChars = 0;
        FS3ETootView_UpdateCharCount(&app->tootView);

        /* Same "belongs to the account being left" reasoning as
         * accountMaxChars above -- the new account's own profile header
         * (VIEWMODE_User, see FS3EApp_ShowOwnProfileHeader) hasn't been
         * fetched yet either. */
        app->accountProfileFetched       = FALSE;
        app->accountProfileLookupPending = FALSE;

        if (app->accountApiBaseUrl) {
            FS3ENetInstanceInfoReq *iiReq =
                FS3ENetInstanceInfoReq_Alloc(app->accountApiBaseUrl);
            if (iiReq)
                FS3EApp_NetSend(FS3ENETQ_INSTANCE_INFO, iiReq, sizeof(*iiReq));
        }
    }

    /* Tell the title bar which account to draw the row-2 icon for -- see
     * TBLAYOUT_AccountAcct. Safe to call before titleBarLayout exists
     * (SetAttrs on a NULL object is a no-op) -- FS3EApp_LoadAccount()
     * runs before window creation, which passes app->accountAcct again
     * as an initial NewObject tag at that point (see main()). A redraw
     * right away shows the (likely blank/placeholder) icon promptly on a
     * fresh login or re-login rather than waiting for some unrelated
     * event to repaint the title bar; FS3EApp_UpdateUserIcon() covers the
     * separate "the avatar bitmap itself just finished loading" redraw. */
    if (app->titleBarLayout) {
        SetAttrs(app->titleBarLayout, TBLAYOUT_AccountAcct,
                 (ULONG)app->accountAcct, TAG_DONE);
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->titleBarLayout, CurrentMainWindow, NULL, 1);
    }

    /* Fetch our own avatar the same way timeline posts do (see
     * FS3ENETQ_FETCH_IMAGE handling in FS3EApp_HandleNetReply) --
     * AvatarImages is keyed by acct, so this reuses the exact same cache
     * entry/scale pipeline; FS3EApp_UpdateUserIcon() picks up the result
     * once the reply arrives. (Previously disabled: the trashed/garbage
     * image data this used to show was the titlebar_userIcon
     * button.gadget's images/bitmap.image wrapper going stale, not this
     * fetch -- see TitleBarLayout_OnRender, which now draws the avatar
     * directly instead.) */
    if (app->avatarImages && app->accountAcct &&
        app->accountAvatarURL && app->accountAvatarURL[0] &&
        !AvatarImages_IsRequested(app->avatarImages, app->accountAcct))
    {
        ULONG reqSize = sizeof(FS3ENetFetchImageReq)
                      + strlen(app->accountAvatarURL) + 1
                      + strlen(app->accountAcct) + 1
                      + strlen(FS3E_CACHE_SUBDIR_USERICONS) + 1;
        FS3ENetFetchImageReq *req =
            FS3ENetFetchImageReq_Alloc(app->accountAvatarURL, app->accountAcct,
                                       FS3E_CACHE_SUBDIR_USERICONS,
                                       (BOOL)app->settings.keepBigUserIcons,
                                       FALSE);

        if (req) {
            if (FS3EApp_NetSend(FS3ENETQ_FETCH_IMAGE, req, reqSize))
            {
                AvatarImages_MarkRequested(app->avatarImages, app->accountAcct);
            }
            /* done by FS3EApp_NetSend if return false because no internet, was freed twice.
               else
                 FreeVec(req);
            */
        }
    }
}

/* -------------------------------------------------------------------------
 * Recursive directory creation (AmigaOS mkdir -p equivalent). Mirrors
 * FS3ECache_MakeDir() in network_fs3e/fs3enet_cache.c -- that one is
 * `static` and private to the network process, so it can't be called from
 * here; same AmigaOS path-splitting rules apply:
 *   "VOL:dir/sub"  -> last '/'  splits "VOL:dir" / "sub"
 *   "VOL:leaf"     -> no '/',   ':' splits volume root "VOL:" / "leaf"
 *   "VOL:"         -> volume/assign root; Lock() tells us if it exists
 * ---------------------------------------------------------------------- */
static BOOL FS3EApp_MakeDirRecursive(const char *path)
{
    BPTR        lock;
    const char *sep;
    char        parent[512];
    ULONG       parentLen;

    lock = Lock(path, SHARED_LOCK);
    if (lock) { UnLock(lock); return TRUE; }

    sep = strrchr(path, '/');
    if (sep) {
        /* Parent is everything before the last '/'. */
        parentLen = (ULONG)(sep - path);
        if (parentLen == 0 || parentLen >= sizeof(parent)) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        if (!FS3EApp_MakeDirRecursive(parent)) return FALSE;
    } else {
        /* No slash: path is "VOL:leaf". Parent is the volume/assign root
         * "VOL:" which must already exist (we can't create a volume). */
        sep = strchr(path, ':');
        if (!sep) return FALSE;  /* relative path with no drive — refuse */
        parentLen = (ULONG)(sep - path + 1);   /* include ':' */
        if (parentLen >= sizeof(parent)) return FALSE;
        memcpy(parent, path, parentLen);
        parent[parentLen] = '\0';
        lock = Lock(parent, SHARED_LOCK);
        if (!lock) return FALSE;   /* volume/assign offline */
        UnLock(lock);
    }

    lock = CreateDir(path);
    if (!lock) {
        /* Another process may have created it between our Lock check and
         * CreateDir — verify before reporting failure. */
        lock = Lock(path, SHARED_LOCK);
        if (!lock) return FALSE;
    }
    UnLock(lock);
    return TRUE;
}

/* Builds "<userDataPath>/account.dat" into buf, creating userDataPath
 * (recursively) first if it doesn't exist yet. userDataPath is anything
 * user-related that isn't disposable cache (unlike settings.cachePath,
 * which FS3ECache_Flush is free to empty) -- see FS3ESettings.userDataPath,
 * defaults to "PROGDIR:.user". Returns FALSE if the directory couldn't be
 * created/reached. */
static BOOL FS3EApp_AccountDatPath(char *buf, ULONG bufSize)
{
    const char *dir = (app->settings.userDataPath && app->settings.userDataPath[0])
                     ? app->settings.userDataPath : "PROGDIR:.user";
    if (!FS3EApp_MakeDirRecursive(dir)) return FALSE;
    snprintf(buf, bufSize, "%s/account.dat", dir);
    return TRUE;
}

/* -------------------------------------------------------------------------
 * Multi-account list -- every account the user has ever logged into, all
 * kept in the single account.dat (see FS3EAPP_ACCOUNTS_MAGIC below), so
 * fs3eloginview.c's acclistGroup can offer one-click switching
 * (FS3EApp_SwitchAccount) without a fresh OAuth round-trip, and so the app
 * reconnects to whichever one was active when it last quit. The currently
 * active account (app->accountXXX fields) is always kept mirrored into
 * this array too -- see FS3EApp_UpsertAccountsList, called from
 * FS3EApp_SaveAccount() below.
 * ---------------------------------------------------------------------- */

static void FS3EApp_FreeAccountEntry(FS3EAccount *a)
{
    if (a->apiBaseUrl)  { FreeVec(a->apiBaseUrl);  a->apiBaseUrl  = NULL; }
    if (a->accessToken) { FreeVec(a->accessToken); a->accessToken = NULL; }
    if (a->displayName) { FreeVec(a->displayName); a->displayName = NULL; }
    if (a->acct)        { FreeVec(a->acct);        a->acct        = NULL; }
    if (a->avatarURL)   { FreeVec(a->avatarURL);   a->avatarURL   = NULL; }
    if (a->accountId)   { FreeVec(a->accountId);   a->accountId   = NULL; }
}

/* Index of the account matching apiBaseUrl+acct, or -1. Matches on both
 * since the same acct name could theoretically exist on two servers. */
static LONG FS3EApp_FindAccountIndex(const char *apiBaseUrl, const char *acct)
{
    ULONG i;
    if (!apiBaseUrl || !acct) return -1;
    for (i = 0; i < app->accountCount; i++) {
        if (app->accounts[i].apiBaseUrl && app->accounts[i].acct &&
            !strcmp(app->accounts[i].apiBaseUrl, apiBaseUrl) &&
            !strcmp(app->accounts[i].acct, acct))
            return (LONG)i;
    }
    return -1;
}

/* Add-or-update one account in app->accounts[] (matched by apiBaseUrl+
 * acct). Called every time FS3EApp_SetAccount() is confirmed/saved, so
 * the list is always current -- a re-login to an already-known account
 * just refreshes its token/displayName/avatar in place rather than
 * growing the array. Silently drops the update if the array is full and
 * this would be a genuinely new account (FS3E_MAX_ACCOUNTS is generous
 * for a personal client, so this is not expected in practice). */
static void FS3EApp_UpsertAccountsList(const char *apiBaseUrl, const char *accessToken,
                                        const char *displayName, const char *acct,
                                        const char *avatarURL, const char *accountId)
{
    LONG idx = FS3EApp_FindAccountIndex(apiBaseUrl, acct);
    FS3EAccount *a;

    if (idx < 0) {
        if (app->accountCount >= FS3E_MAX_ACCOUNTS) {
            return;
        }
        idx = (LONG)app->accountCount++;
    }

    a = &app->accounts[idx];
    FS3EApp_FreeAccountEntry(a);
    a->apiBaseUrl  = NetStrDup(apiBaseUrl);
    a->accessToken = NetStrDup(accessToken);
    a->displayName = NetStrDup(displayName);
    a->acct        = NetStrDup(acct);
    a->avatarURL   = NetStrDup(avatarURL);
    a->accountId   = NetStrDup(accountId);
}

/* First line of the multi-account account.dat format, distinguishing it
 * from every older on-disk format (the original single-account 6-line
 * format, and "FS3EACCOUNTS1", a since-abandoned multi-account format
 * that still wrote accessToken in plaintext) -- all of which stored a
 * token with no machine-key binding at all, and are therefore refused
 * rather than read, see FS3EApp_LoadAccount. This format's accessToken
 * is instead hex+XOR-encoded with FS3EMachineId_GetKey() (see below). */
#define FS3EAPP_ACCOUNTS_MAGIC "FS3EACCOUNTS2"

/* Machine-derived XOR key (see fs3emachineid.h -- NOT cryptography, just a
 * deterrent against a copied account.dat's tokens working verbatim on a
 * different machine), cached for the process lifetime so the underlying
 * device I/O only ever runs once no matter how many accounts get saved. */
static BOOL  s_machineKeyValid = FALSE;
static UBYTE s_machineKey[FS3EMACHINEID_KEYLEN];

/* Arbitrary fixed constant, folded on top of the raw geometry/RDB-derived
 * bytes below. Those source fields are mostly small numbers (sector size,
 * cylinder/head counts, ...) with long runs of zero high-order bytes, so
 * left alone the derived key would visibly encode "which byte came from
 * which geometry field" (an attacker familiar with typical Amiga geometry
 * could guess large chunks of it), and any two machines with the same
 * common sector size would share those same zero bytes. XORing with this
 * constant is just as cheap and removes that structure -- it adds no real
 * secrecy (anyone reading this source has the constant too), it just
 * stops the key from looking meaningful. */
static const UBYTE s_machineKeySalt[FS3EMACHINEID_KEYLEN] = {
    0x4b, 0x92, 0xd1, 0x07, 0x6a, 0xf3, 0x1c, 0x85,
    0x3e, 0xb0, 0x59, 0xc4, 0x2d, 0x97, 0x60, 0xfa
};

/* Not static: friendsh3ep.c's main() calls this too (to trigger key
 * derivation lazily right after the network/thumb processes start) --
 * see the extern declaration there. */
const UBYTE *FS3EApp_MachineKey(void)
{
    if (!s_machineKeyValid) {
        ULONG i;
        FS3EMachineId_GetKey(s_machineKey);
        for (i = 0; i < FS3EMACHINEID_KEYLEN; i++)
            s_machineKey[i] ^= s_machineKeySalt[i];
        s_machineKeyValid = TRUE;
    }
    return s_machineKey;
}

static int FS3EApp_HexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Encodes rawToken as hex(XOR(rawToken, machine key)) into out (a plain
 * printable ASCII string, safe for the line-based account.dat format --
 * XORing a token's raw bytes directly would produce arbitrary bytes,
 * including embedded '\n'/'\0', that RLINE's line-at-a-time FGets() can't
 * round-trip). Truncates rather than overflows if rawToken is implausibly
 * longer than outCap allows. */
static void FS3EApp_EncodeToken(const char *rawToken, char *out, ULONG outCap)
{
    static const char hexDigits[] = "0123456789abcdef";
    const UBYTE *key = FS3EApp_MachineKey();
    ULONG len    = rawToken ? (ULONG)strlen(rawToken) : 0;
    ULONG maxLen = (outCap >= 1) ? (outCap - 1) / 2 : 0;
    ULONG i;

    if (len > maxLen) len = maxLen;

    for (i = 0; i < len; i++) {
        UBYTE b = (UBYTE)rawToken[i] ^ key[i % FS3EMACHINEID_KEYLEN];
        out[i * 2]     = hexDigits[b >> 4];
        out[i * 2 + 1] = hexDigits[b & 0xF];
    }
    out[len * 2] = '\0';
}

/* Reverses FS3EApp_EncodeToken. Tolerant of malformed input (a non-hex
 * character stops decoding right there rather than reading garbage) --
 * this only ever runs on account.dat's own previously-written content, but
 * a hand-edited or corrupted file should degrade to a truncated/wrong
 * token (which then just fails to authenticate) rather than misbehave. */
static void FS3EApp_DecodeToken(const char *hexIn, char *out, ULONG outCap)
{
    const UBYTE *key = FS3EApp_MachineKey();
    ULONG hexLen = hexIn ? (ULONG)strlen(hexIn) : 0;
    ULONG rawLen = hexLen / 2;
    ULONG maxLen = (outCap >= 1) ? outCap - 1 : 0;
    ULONG i;

    if (rawLen > maxLen) rawLen = maxLen;

    for (i = 0; i < rawLen; i++) {
        int hi = FS3EApp_HexNibble(hexIn[i * 2]);
        int lo = FS3EApp_HexNibble(hexIn[i * 2 + 1]);
        if (hi < 0 || lo < 0) { rawLen = i; break; }
        out[i] = (char)(((UBYTE)((hi << 4) | lo)) ^ key[i % FS3EMACHINEID_KEYLEN]);
    }
    out[rawLen] = '\0';
}

/* Save every entry in app->accounts[] (mirroring the currently active
 * account into it first) to <userDataPath>/account.dat: magic line, count,
 * index of the active account (so relaunching reconnects to the same one
 * -- see FS3EApp_LoadAccount), then 6 lines per account.
 * Not static: fs3erequests.c's FS3EApp_HandleNetReply calls this too --
 * see the extern declaration there. */
void FS3EApp_SaveAccount(void)
{
    BPTR f;
    char path[300];
    LONG activeIdx;
    ULONG i;

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;

    /* Keep app->accounts[] current before writing it out. */
    FS3EApp_UpsertAccountsList(app->accountApiBaseUrl, app->accountAccessToken,
                                app->accountDisplayName, app->accountAcct,
                                app->accountAvatarURL, app->accountId);
    activeIdx = FS3EApp_FindAccountIndex(app->accountApiBaseUrl, app->accountAcct);

    if (!FS3EApp_AccountDatPath(path, sizeof(path))) {
        return;
    }
    f = Open(path, MODE_NEWFILE);
    if (!f) return;

    FPuts(f, FS3EAPP_ACCOUNTS_MAGIC); FPuts(f, "\n");
    {
        char numLine[16];
        snprintf(numLine, sizeof(numLine), "%lu", (unsigned long)app->accountCount);
        FPuts(f, numLine); FPuts(f, "\n");
        snprintf(numLine, sizeof(numLine), "%ld", (long)activeIdx);
        FPuts(f, numLine); FPuts(f, "\n");
    }
    for (i = 0; i < app->accountCount; i++) {
        FS3EAccount *a = &app->accounts[i];
        char encTok[1025];
        FS3EApp_EncodeToken(a->accessToken ? a->accessToken : "", encTok, sizeof(encTok));
        FPuts(f, a->apiBaseUrl  ? a->apiBaseUrl  : ""); FPuts(f, "\n");
        FPuts(f, encTok); FPuts(f, "\n");
        FPuts(f, a->displayName ? a->displayName : ""); FPuts(f, "\n");
        FPuts(f, a->acct        ? a->acct        : ""); FPuts(f, "\n");
        FPuts(f, a->avatarURL   ? a->avatarURL   : ""); FPuts(f, "\n");
        FPuts(f, a->accountId   ? a->accountId   : ""); FPuts(f, "\n");
    }
    Close(f);
}

/* Load <userDataPath>/account.dat into app->accounts[]/accountCount, and
 * reconnect to whichever one was active when the app last quit (so a
 * relaunch resumes the same session, not just "the last thing that was
 * ever written"). Returns TRUE if an account is now active.
 *
 * Only the current format (FS3EAPP_ACCOUNTS_MAGIC, token hex+XOR-encoded
 * with the machine key -- see FS3EApp_EncodeToken/FS3EMachineId_GetKey) is
 * ever auto-connected. Anything older -- FS3EAPP_ACCOUNTS_MAGIC_V1 (an
 * earlier multi-account format that still wrote the token in plaintext)
 * or the original single-account 6-line format -- stored its token with
 * no machine binding at all, which is exactly the "an account.dat copied
 * off this machine still works elsewhere" risk the encoding exists to
 * close. Trusting such a file "just this once, then upgrade it" (an
 * earlier version of this function did that) defeats the whole point: it
 * still authenticates with the bare, unbound token before ever
 * re-encoding it. So an unrecognized/older format is refused outright --
 * logged, not connected, not migrated -- and the user has to log back in,
 * which produces a freshly machine-bound entry.
 * Not static: friendsh3ep.c's main() calls this too -- see the extern
 * declaration there. */
BOOL FS3EApp_LoadAccount(void)
{
    BPTR f;
    char path[300];
    char apiBaseUrl[256], tokLine[1025], accessToken[512];
    char displayName[128], acct[128], avatarURL[512], accountId[64];
    ULONG n;

    if (!FS3EApp_AccountDatPath(path, sizeof(path))) return FALSE;

    f = Open(path, MODE_OLDFILE);
    if (!f) return FALSE;

    /* FGets includes the trailing '\n' — strip it. */
#define RLINE(buf) \
    (FGets(f, buf, sizeof(buf)) && (buf[0] != '\0') && \
     ((n = strlen(buf)) > 0) && (buf[n-1] == '\n' ? (buf[n-1] = '\0', 1) : 1))

    if (!RLINE(apiBaseUrl)) { Close(f); return FALSE; }

    if (strcmp(apiBaseUrl, FS3EAPP_ACCOUNTS_MAGIC) != 0) {
        Close(f);
        return FALSE;
    }

    {
        char  numLine[16];
        ULONG count, i;
        LONG  activeIdx;

        if (!RLINE(numLine)) { Close(f); return FALSE; }
        count = (ULONG)atol(numLine);
        if (count > FS3E_MAX_ACCOUNTS) count = FS3E_MAX_ACCOUNTS;

        if (!RLINE(numLine)) { Close(f); return FALSE; }
        activeIdx = (LONG)atol(numLine);

        for (i = 0; i < count; i++) {
            if (!RLINE(apiBaseUrl) || !RLINE(tokLine) || !apiBaseUrl[0] || !tokLine[0])
                break;
            FS3EApp_DecodeToken(tokLine, accessToken, sizeof(accessToken));
            if (!RLINE(displayName)) displayName[0] = '\0';
            if (!RLINE(acct))        acct[0]        = '\0';
            if (!RLINE(avatarURL))   avatarURL[0]   = '\0';
            if (!RLINE(accountId))   accountId[0]   = '\0';

            FS3EApp_UpsertAccountsList(apiBaseUrl, accessToken, displayName,
                                        acct, avatarURL, accountId);
        }
        Close(f);
#undef RLINE


        if (activeIdx < 0 || (ULONG)activeIdx >= app->accountCount) return FALSE;
        {
            FS3EAccount *a = &app->accounts[activeIdx];
            FS3EApp_SetAccount(a->apiBaseUrl, a->accessToken, a->displayName,
                                a->acct, a->avatarURL, a->accountId);
        }
        app->loginPhase = FS3ELOGIN_DONE;
        return TRUE;
    }
}

/* account.dat files saved before accountId existed load with an empty id,
 * which silently disables VIEWMODE_User's fetch (ViewModeTimeline returns
 * FALSE without it). Re-verify the existing access token to backfill it
 * instead of requiring the user to log out and back in -- see
 * FS3ENETQ_VERIFY_ACCOUNT's reply case in FS3EApp_HandleNetReply().
 * Not static: friendsh3ep.c's main() calls this too -- see the extern
 * declaration there. */
void FS3EApp_BackfillAccountId(void)
{
    FS3ENetVerifyAccountReq *req;

    if (!app->accountApiBaseUrl || !app->accountAccessToken) return;
    if (app->accountId && app->accountId[0]) return;

    req = FS3ENetVerifyAccountReq_Alloc(app->accountApiBaseUrl, app->accountAccessToken);
    if (!req) return;

    FS3EApp_NetSend(FS3ENETQ_VERIFY_ACCOUNT, req,
        sizeof(FS3ENetVerifyAccountReq) /* net process only reads char* fields */);
}

/* Rebuild the login window's accounts list from app->accounts[], marking
 * whichever entry matches the currently active account as "current" (see
 * FS3ELoginAccountRow.current). Safe to call any time -- before the
 * window has ever been created it's a no-op (FS3ELoginView_SetAccountsList
 * bails on a NULL acclistBrowser). Call this any time app->accounts[] or
 * the active account changes: after loading accounts.dat at startup,
 * after a successful login/switch, and right before opening the login
 * window (GID_TITLEBAR_ACCOUNTS) so it's never stale.
 * Not static: fs3erequests.c's FS3EApp_HandleNetReply calls this too --
 * see the extern declaration there. */
void FS3EApp_RefreshLoginAccountsList(void)
{
    FS3ELoginAccountRow rows[FS3E_MAX_ACCOUNTS];
    ULONG i;

    for (i = 0; i < app->accountCount; i++) {
        rows[i].server  = app->accounts[i].apiBaseUrl;
        rows[i].user    = app->accounts[i].acct;
        rows[i].current = (app->accountApiBaseUrl && app->accountAcct &&
                           app->accounts[i].apiBaseUrl && app->accounts[i].acct &&
                           !strcmp(app->accounts[i].apiBaseUrl, app->accountApiBaseUrl) &&
                           !strcmp(app->accounts[i].acct, app->accountAcct));
    }
    FS3ELoginView_SetAccountsList(&app->loginView, rows, app->accountCount);
}

/* Switch the connected account to app->accounts[index] (a row click in
 * fs3eloginview.c's acclistGroup -- see GID_LOGIN_ACCOUNTS_LIST). No-op if
 * index is out of range or already the active account. Every toot
 * timeline channel is wiped (they hold the outgoing account's posts) and
 * the current view mode's channel is fetched fresh under the new
 * account, exactly like a brand new login does (FS3ENETQ_LOGIN_FINISH's
 * reply handler below).
 * Not static: friendsh3ep.c's main() calls this too (GID_LOGIN_ACCOUNTS_LIST
 * row click) -- see the extern declaration there. */
void FS3EApp_SwitchAccount(LONG index)
{
    FS3EAccount *a;
    /* Reentrancy guard: FS3ELoginView_SetAccountsList() no longer sets
     * LISTBROWSER_Selected on reattach (that was the confirmed root cause
     * of an infinite switch-A/switch-B loop -- see its comment in
     * fs3eloginview.c), but this guard stays as defense in depth against
     * any *other* BOOPSI notify this function's own side effects might
     * someday trigger back into GID_LOGIN_ACCOUNTS_LIST: the BoopsiDelay
     * queue drains in a single while loop (fs3eboopsimessage.c), so a
     * notify enqueued from inside this call is picked up by that same
     * loop before this call returns, not on some later, safely-separate
     * iteration. */
    static BOOL s_switching = FALSE;

    if (s_switching) {
        return;
    }

    if (index < 0 || (ULONG)index >= app->accountCount) return;
    a = &app->accounts[index];
    if (!a->apiBaseUrl || !a->acct) return;

    if (app->accountApiBaseUrl && app->accountAcct &&
        !strcmp(a->apiBaseUrl, app->accountApiBaseUrl) &&
        !strcmp(a->acct, app->accountAcct))
        return; /* already connected -- see the caller's "not connected yet" gate */

    s_switching = TRUE;


    /* Abandon any fresh-login flow in progress (e.g. the user had pasted
     * a server/started OAuth for yet another account, then changed their
     * mind and clicked an existing row instead). */
    FS3EApp_FreeLoginState();

    FS3EApp_SetAccount(a->apiBaseUrl, a->accessToken, a->displayName,
                        a->acct, a->avatarURL, a->accountId);
    FS3EApp_SaveAccount(); /* account.dat now points at this account too */
    app->loginPhase = FS3ELOGIN_DONE;

    /* Every channel's posts belong to the account being left. */
    if (app->tootTimeline) {
        SetAttrs(app->tootTimeline, TTIMELINE_ClearAllChannels, TRUE, TAG_DONE);
        if (CurrentMainWindow)
            RefreshGList((struct Gadget *)app->tootTimeline, CurrentMainWindow, NULL, 1);
    }

    /* Search-channel profile/discussion state belonged to the outgoing account too. */
    if (app->searchProfileAcct)       { FreeVec(app->searchProfileAcct);       app->searchProfileAcct       = NULL; }
    if (app->searchProfileAccountId)  { FreeVec(app->searchProfileAccountId);  app->searchProfileAccountId  = NULL; }
    if (app->searchDiscussionStatusId){ FreeVec(app->searchDiscussionStatusId);app->searchDiscussionStatusId= NULL; }
    if (app->searchLastQueryText)     { FreeVec(app->searchLastQueryText);     app->searchLastQueryText     = NULL; }
    app->searchMode = FS3ESEARCH_NONE;
    FS3EApp_SearchStackClear(); /* back-navigation history is account-scoped too */

    app->timelineFetchedMask   = 0;
    app->channelPopulatedMask  = 0;
    app->timelineErrorMask     = 0;
    app->channelEmptyMask      = 0;
    app->olderPageInFlightMask = 0;
    app->newerPageInFlightMask = 0;
    FS3EApp_FetchTimeline(app->viewMode);

    FS3EApp_RefreshLoginAccountsList();

    s_switching = FALSE;
}

