#ifndef BOOPSIMESSAGE_H
#define BOOPSIMESSAGE_H

/*
 * BoopsiMessage - Delayed BOOPSI notification queue for Petmate Amiga.
 * Adapted from aukboopsi/boopsimessage.h/.c.
 *
 * A private modelclass instance acts as ICA_TARGET for all our BOOPSI gadgets.
 * When gadgets fire OM_NOTIFY, the dispatcher queues the interesting attributes
 * and signals the main task via SIGBREAKF_CTRL_F. The main event loop then
 * drains the queue after WM_HANDLEINPUT, avoiding deep re-entrant dispatch.
 *
 * Queue format (flat TagItem array):
 *   GA_ID, sender_ID   <- marks start of one message
 *   [tag, value]*      <- optional additional attributes
 *   TAG_END, 0         <- marks end of message
 */

#include <exec/types.h>
#include <utility/tagitem.h>
#include <intuition/classusr.h>

/* Initialize the private modelclass and create the target instance.
 * Returns 1 on success, 0 on failure. */
int  initMessageTargetModel(void);

/* Dispose the target instance and free the modelclass. */
void closeMessageTargetModel(void);

struct BoopsiDelayQueue;
typedef struct BoopsiDelayQueue BoopsiDelayQueue;

/* Global pointers - set by initMessageTargetModel() */
extern BoopsiDelayQueue *DelayQueue;
extern Object           *TargetInstance;

/* Begin a new message (writes GA_ID, sender_ID entry).
 * Returns TRUE if successful. */
BOOL BoopsiDelay_BeginMessage(BoopsiDelayQueue *q, ULONG senderID);

/* Append a tag/value pair to the current message.
 * Returns TRUE if successful. */
BOOL BoopsiDelay_AddTag(BoopsiDelayQueue *q, ULONG tag, ULONG data);

/* End the current message (writes TAG_END entry and sets hasMessages).
 * Returns TRUE if successful. */
BOOL BoopsiDelay_EndMessage(BoopsiDelayQueue *q);

/* Returns TRUE if there are pending messages in the queue. */
BOOL BoopsiDelay_HasMessages(BoopsiDelayQueue *q);

/* Returns pointer to next TagItem message array (starts with GA_ID, ends with TAG_END).
 * Returns NULL and resets the queue when no more messages remain. */
struct TagItem *BoopsiDelay_NextMessage(BoopsiDelayQueue *q);

#endif /* BOOPSIMESSAGE_H */
