#ifndef FS3E_CLIPBOARD_H
#define FS3E_CLIPBOARD_H

/*
 * clipboard.h - Amiga IFF FTXT clipboard write helper for FriendSh3ep.
 */

#include <exec/types.h>

/* Write a NUL-terminated UTF-8 string to the system clipboard (unit 0).
 * Returns TRUE on success. Safe to call with NULL or empty string (no-op). */
BOOL Clipboard_WriteText(const char *text);

#endif /* FS3E_CLIPBOARD_H */
