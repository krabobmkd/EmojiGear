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
#include "fs3erequester.h"

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

    char *newApiBaseUrl, *newAccessToken, *newDisplayName, *newAcct, *newAvatarURL, *newAccountId;

    /* Duplicate every incoming value BEFORE freeing the current fields --
     * a caller is allowed to pass app->accountXXX straight back in as one
     * or more of these parameters (the comment above literally names this
     * case: FS3ENETQ_VERIFY_ACCOUNT's reply handler passes app->
     * accountApiBaseUrl/accountAccessToken back in unchanged, just to
     * refresh the profile fields from a fresh server reply). Freeing
     * app->accountXXX first (the old order) and only THEN calling
     * NetStrDup() on a parameter that pointed at that same memory is a
     * genuine use-after-free: FreeVec()'s free-list bookkeeping writes
     * into the start of the freed block, so the "copy" can read back
     * corrupted/empty bytes instead of the original string. Confirmed as
     * the root cause of a real-account "connects fine, then silently
     * reverts to No account a moment later" regression -- it was always
     * possible, but FS3EApp_VerifyStoredAccount() used to only fire for
     * the rare accountId-backfill case; it now fires on every startup
     * with a real token, so this was hit every single time instead of
     * almost never. */
    newApiBaseUrl  = NetStrDup(apiBaseUrl);
    newAccessToken = NetStrDup(accessToken);
    newDisplayName = NetStrDup(displayName);
    newAcct        = NetStrDup(acct);
    newAvatarURL   = NetStrDup(avatarURL);
    newAccountId   = NetStrDup(accountId);

    FS3EApp_FreeAccount();
    app->accountApiBaseUrl  = newApiBaseUrl;
    app->accountAccessToken = newAccessToken;
    app->accountDisplayName = newDisplayName;
    app->accountAcct        = newAcct;
    app->accountAvatarURL   = newAvatarURL;
    app->accountId          = newAccountId;

    /* Optimistic reset, not a confirmation -- FS3EApp_LoadAccount() and
     * FS3EApp_SwitchAccount() both call this with a locally-stored token
     * that hasn't talked to the server yet, same as the fresh-
     * LOGIN_FINISH/VERIFY_ACCOUNT-reply callers that HAVE just confirmed
     * it. Either way this is a different token/account than whatever
     * accountTokenRejected was last set for, so any earlier rejection no
     * longer applies -- FS3EApp_VerifyStoredAccount() (called right after
     * both LoadAccount and SwitchAccount) will set it again if this one
     * turns out to be dead too. */
    app->accountTokenRejected = FALSE;

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

/* TRUE if the active account can actually post right now -- a real,
 * logged-in account (accountAccessToken non-NULL; NULL is the anonymous-
 * or-no-account convention, see FS3EACCOUNT_ANON_ACCT's doc comment) whose
 * token hasn't been confirmed rejected by the server (accountTokenRejected,
 * see its own doc comment in friendsh3ep.h). Anonymous browsing already
 * blocks Home/Notifications/Search implicitly (FS3EApp_FetchTimeline's
 * Local/Fed carve-out), but composing a toot has no per-channel fetch gate
 * the way those do -- this is that gate for the toot-compose window
 * instead. Shows an EasyRequestArgs error and returns FALSE otherwise.
 * Not static: friendsh3ep.c and fs3eaction.c both call this, right before
 * every FS3ETootView_Open() -- new toot (GID_TITLEBAR_NEWTOOT,
 * Action_NewToot) and every TTL_HOT_REPLY/MODIFY/QUOTE/MESSAGE hotspot --
 * see the extern declaration in fs3eaccounts.h. */
BOOL FS3EApp_RequireRealAccount(void)
{
    if (app->accountAccessToken && !app->accountTokenRejected)
        return TRUE;

    FS3ERequester_Show(CurrentMainWindow, "FriendSh3ep",
        "Connect to a real account to toot.", "OK", FS3EREQ_WARNING);
    ExpungeMessages();
    return FALSE;
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

    /* accountAccessToken may legitimately be NULL here -- an anonymous
     * account (see FS3EACCOUNT_ANON_ACCT in fs3eaccounts.h) still has a
     * real apiBaseUrl and must still be saved, same as any other account;
     * FS3EApp_UpsertAccountsList()/the write loop below both already treat
     * a NULL accessToken as an empty token line via NetStrDup/the "a->
     * accessToken ? ... : \"\"" guards a few lines down. */
    if (!app->accountApiBaseUrl) return;

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
        BOOL  gotBase, gotTok;

        if (!RLINE(numLine)) { Close(f); return FALSE; }
        count = (ULONG)atol(numLine);
        if (count > FS3E_MAX_ACCOUNTS) count = FS3E_MAX_ACCOUNTS;

        if (!RLINE(numLine)) { Close(f); return FALSE; }
        activeIdx = (LONG)atol(numLine);

        for (i = 0; i < count; i++) {
            /* tokLine legitimately empty (an anonymous account, see
             * FS3EACCOUNT_ANON_ACCT) still means RLINE() itself succeeded
             * -- only !gotBase/!gotTok (FGets hit real EOF/read failure)
             * or an empty apiBaseUrl (always required, anonymous or not)
             * signal a truncated/corrupt file worth stopping on. An
             * earlier version of this loop also broke on `!tokLine[0]`,
             * which silently discarded an anonymous entry AND every
             * account listed after it in the file. */
            gotBase = RLINE(apiBaseUrl);
            gotTok  = RLINE(tokLine);
            if (!gotBase || !gotTok || !apiBaseUrl[0])
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

/* Re-verifies the active account's token every launch (not just when
 * accountId is missing, which used to be this function's only job -- see
 * account.dat files saved before accountId existed, which load with an
 * empty id and silently disable VIEWMODE_User's fetch, ViewModeTimeline
 * returning FALSE without it). FS3EApp_LoadAccount() only decrypts and
 * trusts account.dat locally -- it never talks to the server -- so without
 * this, a token revoked/expired server-side after that file was written
 * would sit there looking "Account connected." indefinitely (the
 * accountId-missing case is genuinely rare -- an upgrade from an old
 * account.dat format -- so re-verifying unconditionally instead is one
 * extra request at startup, not per-launch overhead worth gating on it).
 * See FS3ENETQ_VERIFY_ACCOUNT's reply case in FS3EApp_HandleNetReply() for
 * what happens to app->accountTokenRejected on a confirmed rejection.
 * Not static: friendsh3ep.c's main() calls this too -- see the extern
 * declaration there. */
void FS3EApp_VerifyStoredAccount(void)
{
    FS3ENetVerifyAccountReq *req;

    /* A NULL accessToken here is a deliberately anonymous account (see
     * FS3EACCOUNT_ANON_ACCT in fs3eaccounts.h) -- there is no token to
     * verify, and firing this anyway would send verify_credentials an
     * empty Bearer header, which the server rejects just like a dead
     * token would, wrongly flipping app->accountTokenRejected TRUE for
     * an account that was never logged in to begin with. */
    if (!app->accountApiBaseUrl || !app->accountAccessToken)
        return;

    req = FS3ENetVerifyAccountReq_Alloc(app->accountApiBaseUrl, app->accountAccessToken);
    if (!req) return;

    FS3EApp_NetSend(FS3ENETQ_VERIFY_ACCOUNT, req,
        sizeof(FS3ENetVerifyAccountReq) /* net process only reads char* fields */);
}

/* Instances confirmed by hand (2026-08-06) to allow anonymous timeline
 * preview -- see FS3ENETR_AUTH_REQUIRED's doc comment in fs3enet.h for why
 * that isn't true of every public Mastodon instance (mastodon.social
 * notably does not, despite otherwise being TLS-reachable). First entry
 * becomes the initially-connected account (see this function's doc
 * comment in fs3eaccounts.h); the rest are just pre-populated into the
 * accounts list so a first-time user has a pick of already-known-good
 * servers to switch to (GID_LOGIN_ACCOUNTS_LIST) instead of having to
 * discover which random instance allows this by trial and error. */
static const char *const s_defaultAnonServers[] = {
    "https://mas.to",
    "https://mastodon.world",
    "https://mstdn.social",
    "https://masto.nu",
    "https://wehavecookies.social",
    "https://piaille.fr",
    "https://mastoot.fr",
    "https://mastodon.uno",
    "https://infosec.exchange",
};
#define FS3E_DEFAULT_ANON_SERVER_COUNT \
    (sizeof(s_defaultAnonServers) / sizeof(s_defaultAnonServers[0]))

void FS3EApp_SeedDefaultAnonymousAccount(void)
{
    ULONG i;

    /* Upsert every candidate into app->accounts[] first (all sharing the
     * FS3EACCOUNT_ANON_ACCT sentinel, told apart by apiBaseUrl -- see its
     * own doc comment) without going through FS3EApp_SetAccount() for each
     * one, which would otherwise fire a FS3ENETQ_INSTANCE_INFO request and
     * bump accountGeneration per server for 8 servers nobody has looked at
     * yet. That happens naturally, once, whenever the user actually
     * switches to one of them (FS3EApp_SetAccount()'s own "!sameAccount"
     * branch). */
    for (i = 0; i < FS3E_DEFAULT_ANON_SERVER_COUNT; i++)
        FS3EApp_UpsertAccountsList(s_defaultAnonServers[i], "", "",
                                    FS3EACCOUNT_ANON_ACCT, "", "");

    FS3EApp_SetAccount(s_defaultAnonServers[0], "", "",
                        FS3EACCOUNT_ANON_ACCT, "", "");
    FS3EApp_SaveAccount();
}

/* One row of FS3EApp_RefreshLoginAccountsList()'s own before/after
 * comparison -- server/user copied (AllocVec'd) rather than borrowed,
 * since app->accounts[]'s own strings can be freed/reallocated in place
 * (FS3EApp_UpsertAccountsList) between calls, so a stale borrowed pointer
 * from a previous call can't be trusted to still point at the same bytes,
 * let alone compared by address. */
typedef struct {
    char *server;
    char *user;
    BOOL  current;
} FS3ELoginListSnapshotRow;

static FS3ELoginListSnapshotRow s_lastAccountRows[FS3E_MAX_ACCOUNTS];
static ULONG s_lastAccountRowCount = 0;
static BOOL  s_haveAccountRowSnapshot = FALSE;

/* TRUE if rows[0..count-1] differs from the snapshot left by the last
 * call that actually rebuilt the list (fs3eaccounts_snapshot_rows below)
 * -- count itself, or any row's server/user/current. NULL and "" compare
 * equal (both normalized to "" below) since FS3EAccount fields are
 * NetStrDup'd, which already collapses "" to NULL (see FS3EACCOUNT_ANON_
 * ACCT's doc comment in fs3eaccounts.h) -- without this, an anonymous
 * account's row would spuriously compare "changed" against itself. */
static BOOL fs3eaccounts_rows_changed(const FS3ELoginAccountRow *rows, ULONG count)
{
    ULONG i;

    if (!s_haveAccountRowSnapshot || count != s_lastAccountRowCount) return TRUE;

    for (i = 0; i < count; i++) {
        const char *prevServer = s_lastAccountRows[i].server ? s_lastAccountRows[i].server : "";
        const char *prevUser   = s_lastAccountRows[i].user   ? s_lastAccountRows[i].user   : "";
        const char *newServer  = rows[i].server ? rows[i].server : "";
        const char *newUser    = rows[i].user   ? rows[i].user   : "";

        if (s_lastAccountRows[i].current != rows[i].current) return TRUE;
        if (strcmp(prevServer, newServer) != 0) return TRUE;
        if (strcmp(prevUser, newUser) != 0) return TRUE;
    }
    return FALSE;
}

/* Replaces s_lastAccountRows[] with a fresh AllocVec'd copy of rows[0..
 * count-1] -- called only once FS3ELoginView_SetAccountsList() has
 * actually been told about this exact state, so the snapshot always
 * reflects what the gadget was last asked to show. */
static void fs3eaccounts_snapshot_rows(const FS3ELoginAccountRow *rows, ULONG count)
{
    ULONG i;

    for (i = 0; i < s_lastAccountRowCount; i++) {
        if (s_lastAccountRows[i].server) { FreeVec(s_lastAccountRows[i].server); s_lastAccountRows[i].server = NULL; }
        if (s_lastAccountRows[i].user)   { FreeVec(s_lastAccountRows[i].user);   s_lastAccountRows[i].user   = NULL; }
    }

    for (i = 0; i < count && i < FS3E_MAX_ACCOUNTS; i++) {
        s_lastAccountRows[i].server  = NetStrDup(rows[i].server);
        s_lastAccountRows[i].user    = NetStrDup(rows[i].user);
        s_lastAccountRows[i].current = rows[i].current;
    }
    s_lastAccountRowCount    = count;
    s_haveAccountRowSnapshot = TRUE;
}

/* Rebuild the login window's accounts list from app->accounts[], marking
 * whichever entry matches the currently active account as "current" (see
 * FS3ELoginAccountRow.current -- TRUE as soon as a switch/login has been
 * ASKED for, not once it's confirmed working: FS3EApp_SetAccount() sets
 * accountApiBaseUrl/accountAcct synchronously, before any network round-
 * trip, so this is already "optimistic" with no extra state needed).
 *
 * Skips touching the listbrowser at all -- no LISTBROWSER_Labels detach/
 * reattach -- if the computed rows (content AND which one is current)
 * are byte-for-byte identical to what the gadget was last actually told
 * to show (see fs3eaccounts_rows_changed() above). This matters beyond
 * just avoiding needless work: a detach/reattach the gadget didn't need
 * is exactly what provokes its own self-triggered LISTBROWSER_Selected
 * notify (see FS3ELoginView_SetAccountsList()'s own comment in
 * fs3eloginview.c) -- callers that call this defensively/repeatedly
 * (e.g. right before opening the login window, "just in case it's stale")
 * no longer risk kicking that off for nothing.
 *
 * Safe to call any time -- before the window has ever been created it's
 * still a no-op past this guard (FS3ELoginView_SetAccountsList itself
 * bails on a NULL acclistBrowser; this function deliberately does NOT
 * snapshot in that case, so the first call after the gadget really exists
 * always goes through instead of comparing against a pre-gadget snapshot
 * and wrongly deciding nothing changed). Call this any time
 * app->accounts[] or the active account changes: after loading
 * accounts.dat at startup, after a successful login/switch, and right
 * before opening the login window (GID_TITLEBAR_ACCOUNTS) so it's never
 * stale.
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

    if (app->loginView.acclistBrowser && !fs3eaccounts_rows_changed(rows, app->accountCount))
        return;

    if (app->loginView.acclistBrowser)
        fs3eaccounts_snapshot_rows(rows, app->accountCount);

    FS3ELoginView_SetAccountsList(&app->loginView, rows, app->accountCount);
}

/* Wipes every bit of state that belonged to whichever account was just
 * left (toot timeline channels, search-channel profile/discussion state,
 * every fetch-tracking bitmask) and fetches the current view mode's
 * channel fresh under whatever account is active now (possibly none) --
 * shared by FS3EApp_SwitchAccount(), FS3EApp_DeleteSelectedAccount() and
 * FS3EApp_ConnectAnonymously() (fs3erequests.c), all of which change the
 * active account (or clear it entirely) and need the exact same "nothing
 * from the old account should linger on screen" reset. Not static: see
 * the extern declaration in fs3eaccounts.h. */
void FS3EApp_ResetPerAccountState(void)
{
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

    /* VIEWMODE_User is excluded from FS3EApp_FetchTimeline's generic
     * mechanism (see that function's own comment) -- it's normally
     * (re)populated by fs3e_setViewMode() calling
     * FS3EApp_ShowOwnProfileHeader() whenever the User tab is switched
     * TO. But an account switch while ALREADY sitting on VIEWMODE_User
     * never goes through fs3e_setViewMode() (the view mode itself isn't
     * changing), so that call site is missed; accountProfileFetched was
     * just reset to FALSE above (via FS3EApp_SetAccount) with nothing
     * left to act on it. Mirror that call here so the profile actually
     * reloads under the new account instead of only catching up the next
     * time the user leaves and re-enters VIEWMODE_User. */
    if (app->viewMode == VIEWMODE_User)
        FS3EApp_ShowOwnProfileHeader();
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

    if(app->accountCurrentlyConnecting == index) return;
    app->accountCurrentlyConnecting = index;

    if (app->accountApiBaseUrl && app->accountAcct &&
        !strcmp(a->apiBaseUrl, app->accountApiBaseUrl) &&
        !strcmp(a->acct, app->accountAcct))
        {
    //    printf("FS3EApp_SwitchAccount quit because already connected\n");
        return; /* already connected -- see the caller's "not connected yet" gate */
        }

    s_switching = TRUE;


    /* Abandon any fresh-login flow in progress (e.g. the user had pasted
     * a server/started OAuth for yet another account, then changed their
     * mind and clicked an existing row instead). */
    FS3EApp_FreeLoginState();

    FS3EApp_SetAccount(a->apiBaseUrl, a->accessToken, a->displayName,
                        a->acct, a->avatarURL, a->accountId);
    FS3EApp_SaveAccount(); /* account.dat now points at this account too */
    app->loginPhase = FS3ELOGIN_DONE;

    /* This entry's token came straight from account.dat too, same
     * unverified state a saved account is in at startup -- see
     * FS3EApp_VerifyStoredAccount()'s own doc comment. */
    FS3EApp_VerifyStoredAccount();

    FS3EApp_ResetPerAccountState(); /* every channel's posts belong to the account being left */

 /*just need to select the correct, not recreate the list
    FS3EApp_RefreshLoginAccountsList();
  */
    FS3EApp_SelectListIndex(&app->loginView,index);

    s_switching = FALSE;
}

/* GID_LOGIN_DELETE_SERVER_BUTTON -- forgets the currently active account
 * outright (removed from app->accounts[]/account.dat, not just switched
 * away from like FS3EApp_SwitchAccount()), then connects to whichever
 * account is now first in the list -- same "nothing left on screen from
 * the old account" reset FS3EApp_SwitchAccount() applies. No-op if there's
 * no active account to delete.
 * Not static: friendsh3ep.c's main() calls this too -- see the extern
 * declaration there. */
void FS3EApp_DeleteSelectedAccount(void)
{
    LONG  idx;
    ULONG i;

    if (!app->accountApiBaseUrl || !app->accountAcct) return;

    idx = FS3EApp_FindAccountIndex(app->accountApiBaseUrl, app->accountAcct);
    if (idx < 0) return; /* shouldn't happen -- the active account is always mirrored into accounts[] */

    /* Same "the user changed their mind" reasoning as FS3EApp_SwitchAccount(). */
    FS3EApp_FreeLoginState();

    FS3EApp_FreeAccountEntry(&app->accounts[idx]);
    for (i = (ULONG)idx; i + 1 < app->accountCount; i++)
        app->accounts[i] = app->accounts[i + 1]; /* shallow copy -- shifts pointer ownership down one slot */
    app->accountCount--;
    memset(&app->accounts[app->accountCount], 0, sizeof(FS3EAccount)); /* the now-unused tail slot */

    FS3EApp_FreeAccount(); /* the active-account mirror -- it's gone */
    app->accountTokenRejected = FALSE;
    app->loginPhase = FS3ELOGIN_IDLE;

    if (app->accountCount > 0) {
        FS3EAccount *a = &app->accounts[0];
        FS3EApp_SetAccount(a->apiBaseUrl, a->accessToken, a->displayName,
                            a->acct, a->avatarURL, a->accountId);
        app->loginPhase = FS3ELOGIN_DONE;
        /* This entry's token came straight from account.dat too, same
         * unverified state a saved account is in at startup -- see
         * FS3EApp_VerifyStoredAccount()'s own doc comment. */
        FS3EApp_VerifyStoredAccount();
        FS3EApp_SaveAccount();
    } else {
        /* Nothing left -- clear account.dat outright rather than leave a
         * stale file still naming the just-deleted account behind:
         * FS3EApp_SaveAccount() itself won't write anything here anyway
         * (it requires an active account), and next launch's
         * FS3EApp_LoadAccount() finding no file at all is exactly what
         * re-triggers FS3EApp_SeedDefaultAnonymousAccount() in main(),
         * same as a genuine first launch. */
        char path[300];
        if (FS3EApp_AccountDatPath(path, sizeof(path)))
            DeleteFile((STRPTR)path);
    }

    FS3EApp_ResetPerAccountState();


    FS3EApp_RefreshLoginAccountsList();

    app->accountCurrentlyConnecting = -1;
}

