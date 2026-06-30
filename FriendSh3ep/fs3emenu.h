/*
 * fs3emenu.h - GadTools menu management for FriendSh3ep.
 *
 * nm_UserData encoding (same scheme as EmojiGear/egmenu.h):
 *   NM_TITLE items: nm_UserData = MSG_* locale id  (upper 16 bits zero)
 *   NM_ITEM  items: nm_UserData = ACTION_UD(FS3EACTION_*) | MSG_* label id
 *
 * Pattern adapted from EmojiGear/egmenu.h.
 */

#ifndef FS3EMENU_H
#define FS3EMENU_H

#include <exec/types.h>
#include <intuition/intuition.h>

/* Action IDs live in fs3eaction.h; import them so callers only need fs3emenu.h */
#include "fs3eaction.h"

/* Menu state */
typedef struct FS3EMenu {
    struct Menu *menu;
    APTR         visualInfo;
} FS3EMenu;

/* Create menus and attach to window. Returns TRUE on success. */
BOOL FS3EMenu_Create(FS3EMenu *m, struct Screen *screen, struct Window *window);

/* Remove menus from window and free all resources. */
void FS3EMenu_Close(FS3EMenu *m, struct Window *window);

/* Close and recreate menus (e.g. after screen mode change). */
BOOL FS3EMenu_Rebuild(FS3EMenu *m, struct Screen *screen, struct Window *window);

/* Translate a menu selection code (result & WMHI_MENUMASK) to a FS3EACTION_*
 * id. Returns -1 if no valid action item was selected. */
LONG FS3EMenu_ToActionID(FS3EMenu *m, UWORD menuCode);

#endif /* FS3EMENU_H */
