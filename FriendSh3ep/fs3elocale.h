/*
 * fs3elocale.h - localization support for FriendSh3ep.
 *
 * Uses locale.library catalog if available, falls back to built-in
 * English strings. LocaleBase must be declared in friendsh3ep.c.
 *
 * Pattern adapted from EmojiGear/eglocale.h.
 */

#ifndef FS3ELOCALE_H
#define FS3ELOCALE_H

#include <exec/types.h>

/* String IDs - must stay in the same order as defaultStrings[] in fs3elocale.c */
enum {
    /* Main window */
    MSG_WINDOW_TITLE = 0,
    MSG_LOGIN_BUTTON,
    MSG_NEWTOOT_BUTTON,

    /* Login window (fs3eloginview.c) */
    MSG_LOGIN_TITLE,    /* window title and group label */
    MSG_LOGIN_SERVER,
    MSG_LOGIN_USER,
    MSG_LOGIN_CODE,
    MSG_LOGIN_LOGIN,

    /* New toot window (fs3etootview.c) */
    MSG_TOOT_TITLE,
    MSG_TOOT_SUBJECT,
    MSG_TOOT_VISIBILITY_PUBLIC,
    MSG_TOOT_VISIBILITY_UNLISTED,
    MSG_TOOT_VISIBILITY_PRIVATE,
    MSG_TOOT_VISIBILITY_DIRECT,
    MSG_TOOT_SEND,
    MSG_TOOT_CHARS_FORMAT, /* format: "%lu chars" */

    /* Must be last */
    MSG_COUNT
};

/* Initialize locale system. catalogName may be NULL (English only). */
BOOL FS3ELocale_Init(const char *catalogName, ULONG version);

/* Close catalog and free resources. */
void FS3ELocale_Close(void);

/* Return localized string by ID. Falls back to built-in English. */
const char *FS3ELocale_GetString(ULONG stringID);

/* Convenience macro */
#define LOC(id) FS3ELocale_GetString(id)

#endif /* FS3ELOCALE_H */
