#ifndef FS3EBOOPSIMESSAGE_H
#define FS3EBOOPSIMESSAGE_H

/*
 * FS3EBoopsiMessage - delayed BOOPSI notification queue for FriendSh3ep.
 * Adapted from EmojiGear/boopsimessage.h/.c.
 *
 * A private unnamed modelclass instance (TargetInstance) is created and
 * used as ICA_TARGET for our BOOPSI gadgets (buttons, string gadgets, ...).
 * Gadgets may fire OM_NOTIFY from a task other than the GUI's main process
 * (e.g. layout's helper process); the dispatcher below queues the attributes
 * we care about and signals the main process (myTask) via SIGBREAKF_CTRL_F
 * so that the actual handling happens safely on the main task, after
 * WM_HANDLEINPUT in the main loop.
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
int  FS3EMsg_Init(void);

/* Dispose the target instance and free the modelclass. */
void FS3EMsg_Close(void);

struct BoopsiDelayQueue;
typedef struct BoopsiDelayQueue BoopsiDelayQueue;

/* Global pointers - set by FS3EMsg_Init() */
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

/* Returns pointer to next TagItem message array (starts with GA_ID, ends with
 * TAG_END). Returns NULL and resets the queue when no more messages remain. */
struct TagItem *BoopsiDelay_NextMessage(BoopsiDelayQueue *q);

#endif /* FS3EBOOPSIMESSAGE_H */
