#ifndef FS3EACCOUNTS_H
#define FS3EACCOUNTS_H

/*
 * fs3eaccounts.c - multi-account list persistence and the active-account
 * state (app->accountXXX fields), split out of friendsh3ep.c. See
 * friendsh3ep.h for struct App/FS3EAccount.
 */

/* FS3EAccount.acct sentinel for an anonymous connection (apiBaseUrl set,
 * accessToken NULL). Callers construct one by passing "" as accessToken to
 * FS3EApp_SetAccount()/FS3EApp_UpsertAccountsList() -- NetStrDup("") (see
 * fs3enetworkhelper.c) collapses any empty string to NULL on the way in,
 * so accountAccessToken/FS3EAccount.accessToken end up NULL either way.
 * That NULL is the app-wide "not logged in" signal already used by
 * fs3etootview.c/fs3eaction.c's `accessToken && accessToken[0]` gates
 * (which is why passing "" rather than NULL still works there -- both
 * collapse to the same falsy NULL) -- there is deliberately no separate
 * "empty but non-NULL" state anywhere in this codebase to preserve, so
 * every check added for this feature tests plain `!accountAccessToken`,
 * not `!accountAccessToken[0]` (which would be a NULL-pointer dereference
 * once NetStrDup has done its collapsing). What *does* distinguish "never
 * logged in at all" from "deliberately anonymous" is accountApiBaseUrl:
 * NULL for the former, a real server for the latter (see
 * FS3EApp_CheckConnectionState in friendsh3ep.c).
 *
 * A real Mastodon "acct" from the API is never "@"-prefixed (bare
 * "username" for a local account, "username@domain" for a remote one), so
 * this sentinel can't collide with a genuine account -- and it lets
 * FS3EApp_FindAccountIndex()/FS3EApp_SwitchAccount() treat every anonymous
 * server as its own distinct, selectable app->accounts[] row (they key on
 * apiBaseUrl+acct, so two anonymous servers sharing this same acct are
 * still told apart by apiBaseUrl). This constant only exists for the
 * couple of call sites that construct a fresh anonymous entry
 * (FS3EApp_ConnectAnonymously in fs3erequests.c,
 * FS3EApp_SeedDefaultAnonymousAccount below), so they don't hand-type the
 * string differently. */
#define FS3EACCOUNT_ANON_ACCT "@anonymous"

/* Free the currently active account's credentials (app->accountXXX). */
void FS3EApp_FreeAccount(void);

/* Store account credentials from a LOGIN_FINISH/VERIFY_ACCOUNT reply as the
 * active account, mirroring them into app->accounts[] too. */
void FS3EApp_SetAccount(const char *apiBaseUrl, const char *accessToken,
                         const char *displayName, const char *acct,
                         const char *avatarURL, const char *accountId);

/* TRUE if the active account is a real, working login (not anonymous, not
 * a rejected/expired token) -- otherwise shows an EasyRequestArgs error
 * ("Connect to a real account to toot.") and returns FALSE. Call before
 * every FS3ETootView_Open() -- new toot, reply, edit, quote, message --
 * and bail out on FALSE instead of opening the compose window. */
BOOL FS3EApp_RequireRealAccount(void);

/* Machine-derived XOR key used to obfuscate saved access tokens -- see
 * fs3eaccounts.c for the full doc comment. */
const UBYTE *FS3EApp_MachineKey(void);

/* Persist app->accounts[] (mirroring the active account into it first) to
 * <userDataPath>/account.dat. */
void FS3EApp_SaveAccount(void);

/* Load <userDataPath>/account.dat into app->accounts[] and reconnect to
 * whichever account was active when the app last quit. */
BOOL FS3EApp_LoadAccount(void);

/* First-launch bootstrap: call from main() only when FS3EApp_LoadAccount()
 * left app->accountApiBaseUrl NULL (no account.dat at all, or an
 * unreadable/unsupported one -- either way there's nothing to connect to
 * yet). Seeds a handful of anonymous connections to well-known public
 * instances (s_defaultAnonServers, fs3eaccounts.c) and saves them, so a
 * first-time user sees real toots (Local/Federated) immediately instead of
 * an empty "No account" placeholder, without having to register an app/
 * OAuth-login before they've even decided if they like the client -- and
 * has a pick of already-known-good servers in the accounts list instead of
 * just the one that happens to be first. */
void FS3EApp_SeedDefaultAnonymousAccount(void);

/* Re-verify the active account's token against the server -- backfills
 * accountId if it's missing (account.dat saved before accountId existed)
 * and, on a confirmed rejection, sets app->accountTokenRejected so
 * FS3EApp_CheckConnectionState() can tell the user their token is dead
 * instead of just showing "Account connected." forever (see
 * accountTokenRejected's doc comment in friendsh3ep.h). Called once at
 * startup right after FS3EApp_LoadAccount(). */
void FS3EApp_VerifyStoredAccount(void);

/* Rebuild the login window's accounts list from app->accounts[]. */
void FS3EApp_RefreshLoginAccountsList(void);

/* Wipes every bit of state that belonged to whichever account was just
 * left/changed away from (toot timeline channels, search-channel state,
 * every fetch-tracking bitmask) and fetches the current view mode's
 * channel fresh under whichever account is active now. Call this any time
 * app->accountXXX changes outside of FS3EApp_SwitchAccount()/
 * FS3EApp_DeleteSelectedAccount() (which already call it themselves) --
 * e.g. FS3EApp_ConnectAnonymously() in fs3erequests.c. */
void FS3EApp_ResetPerAccountState(void);

/* Switch the connected account to app->accounts[index]. */
void FS3EApp_SwitchAccount(LONG index);

/* Forget the currently active account outright (removed from
 * app->accounts[]/account.dat), then connect to whichever account is now
 * first in the list, if any -- see GID_LOGIN_DELETE_SERVER_BUTTON. */
void FS3EApp_DeleteSelectedAccount(void);

#endif /* FS3EACCOUNTS_H */
