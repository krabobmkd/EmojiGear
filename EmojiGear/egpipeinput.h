#ifndef EGPIPEINPUT_H
#define EGPIPEINPUT_H

/*
 * Shared pipe / stdin input handling for EmojiGear and MUImojiGear.
 *
 * AmigaDOS pipes and file redirections are not interactive, so Read()
 * returns immediately with whatever bytes are currently available (possibly
 * zero) instead of blocking.  EgPipeInput_Poll() wraps the non-blocking
 * read, keeps multi-byte UTF-8 sequences from being split across calls, and
 * transparently re-encodes plain 8-bit Latin-1 ("ANSI") input to UTF-8
 * before it is handed to the text editor.
 */

#include <exec/types.h>

/* Read whatever is available from Input() (only when it is a non-interactive
 * handle, i.e. PIPE: or a file redirection) into buf, which has capacity
 * bufSize and currently holds *pUsed bytes.  Returns a NUL-terminated UTF-8
 * chunk covering the largest complete-codepoint prefix of the buffer, ready
 * to be passed to UTED_InsertText, or NULL if there is nothing to insert
 * this iteration.
 *
 * Any incomplete trailing multi-byte sequence is shifted to the front of buf
 * and *pUsed is updated accordingly so it can be completed on a later call.
 *
 * If *needsFree is set to TRUE on return, the caller must FreeVec() the
 * returned pointer once done with it.  Otherwise the pointer aliases buf and
 * is only valid until the next call to EgPipeInput_Poll(). */
const char *EgPipeInput_Poll(char *buf, ULONG bufSize, LONG *pUsed, BOOL *needsFree);

#endif /* EGPIPEINPUT_H */
