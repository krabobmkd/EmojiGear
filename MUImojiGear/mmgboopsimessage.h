#ifndef MMGBOOPSIMESSAGE_H
#define MMGBOOPSIMESSAGE_H

/*
 * mmgboopsimessage.h – Delayed BOOPSI notification queue for MUImojiGear.
 *
 * Adapted from EmojiGear/boopsimessage.c.
 *
 * A private unnamed modelclass is created and its single instance is used
 * as ICA_TARGET for the embedded UniTextEditor BOOPSI gadget.
 * The dispatcher (input.device context) queues UTED_* attributes
 * into a flat TagItem ring and signals mmgMainTask via SIGBREAKF_CTRL_F.
 * The MUI event loop drains the queue in the safe application-task context.
 *
 * Queue format (flat TagItem array, parsed with FindTagItem):
 *   GA_ID,  senderID   <- marks start of one notification record
 *   [tag,   value]*    <- interesting attribute/value pairs
 *   TAG_END, 0         <- marks end of record
 */

#include <exec/types.h>
#include <utility/tagitem.h>
#include <intuition/classusr.h>

/* Initialise the private modelclass and allocate the target instance.
 * Must be called after IntuitionBase is open. Returns 1 on success. */
int  MmgBoopsi_Init(void);

/* Dispose the target instance and free the modelclass. */
void MmgBoopsi_Close(void);

/* Opaque queue type */
struct MmgDelayQueue;
typedef struct MmgDelayQueue MmgDelayQueue;

/* Set by MmgBoopsi_Init(). Use as ICA_TARGET for every BOOPSI gadget. */
extern Object          *MmgTargetInstance;

/* Set by MmgBoopsi_Init(). Use BoopsiDelay_* to drain notifications. */
extern MmgDelayQueue   *MmgQueue;

/* Returns TRUE when at least one notification record is pending. */
BOOL MmgBoopsi_HasMessages(void);

/* Returns a pointer to the first TagItem of the next notification record
 * (starting with GA_ID), or NULL when the queue is empty and has been reset.
 * The returned pointer is valid until the next MmgBoopsi_NextMessage() call. */
struct TagItem *MmgBoopsi_NextMessage(void);

#endif /* MMGBOOPSIMESSAGE_H */
