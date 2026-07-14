#ifndef FS3EREQUESTS_H
#define FS3EREQUESTS_H

/*
 * fs3erequests.c - network/thumbnail request construction and async reply
 * dispatch, split out of friendsh3ep.c. See friendsh3ep.h for struct App
 * and the shared application state these functions read/write.
 */

#include "network_fs3e/fs3enet.h"
#include "fs3ethumb.h"

/* Send a pre-allocated request block to the network process asynchronously.
 * See fs3erequests.c for the full doc comment. */
BOOL FS3EApp_NetSend(ULONG type, APTR data, ULONG dataLen);

/* Fire the initial fetch for viewMode, if credentials are available and one
 * hasn't already been started for that channel. */
void FS3EApp_FetchTimeline(ULONG viewMode);

/* Fire an older/newer pagination fetch for viewMode. */
void FS3EApp_FetchTimelinePage(ULONG viewMode, ULONG direction);

/* Open (or re-open) a user's profile in the Search channel. */
void FS3EApp_OpenProfile(const char *acctOrHandle);

/* "Discussion mode" -- show statusId's toot plus its replies in the Search
 * channel. */
void FS3EApp_OpenDiscussion(const char *statusId);

/* Word/hashtag search in the Search channel. */
void FS3EApp_SearchWord(const char *query);

/* GID_LOGIN_LOGIN_BUTTON -- start a fresh OAuth flow for the server typed
 * into the login window. */
void FS3EApp_LoginStart(void);

/* GID_LOGIN_SUBMIT_CODE_BUTTON -- exchange the pasted OOB code for an
 * access token. */
void FS3EApp_LoginSubmitCode(void);

/* Handle one reply message from the network process. */
void FS3EApp_HandleNetReply(FS3ENetMessage *msg);

/* Handle one reply message from the thumbnail process. */
void FS3EApp_HandleThumbReply(FS3EThumbMessage *msg);

#endif /* FS3EREQUESTS_H */
