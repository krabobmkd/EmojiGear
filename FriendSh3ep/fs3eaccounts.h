#ifndef FS3EACCOUNTS_H
#define FS3EACCOUNTS_H

/*
 * fs3eaccounts.c - multi-account list persistence and the active-account
 * state (app->accountXXX fields), split out of friendsh3ep.c. See
 * friendsh3ep.h for struct App/FS3EAccount.
 */

/* Free the currently active account's credentials (app->accountXXX). */
void FS3EApp_FreeAccount(void);

/* Store account credentials from a LOGIN_FINISH/VERIFY_ACCOUNT reply as the
 * active account, mirroring them into app->accounts[] too. */
void FS3EApp_SetAccount(const char *apiBaseUrl, const char *accessToken,
                         const char *displayName, const char *acct,
                         const char *avatarURL, const char *accountId);

/* Machine-derived XOR key used to obfuscate saved access tokens -- see
 * fs3eaccounts.c for the full doc comment. */
const UBYTE *FS3EApp_MachineKey(void);

/* Persist app->accounts[] (mirroring the active account into it first) to
 * <userDataPath>/account.dat. */
void FS3EApp_SaveAccount(void);

/* Load <userDataPath>/account.dat into app->accounts[] and reconnect to
 * whichever account was active when the app last quit. */
BOOL FS3EApp_LoadAccount(void);

/* Re-verify the active account's token to backfill accountId if it's
 * missing (accounts.dat saved before accountId existed). */
void FS3EApp_BackfillAccountId(void);

/* Rebuild the login window's accounts list from app->accounts[]. */
void FS3EApp_RefreshLoginAccountsList(void);

/* Switch the connected account to app->accounts[index]. */
void FS3EApp_SwitchAccount(LONG index);

#endif /* FS3EACCOUNTS_H */
