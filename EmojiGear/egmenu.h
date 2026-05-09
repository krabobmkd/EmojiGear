/*
 * Egmenu.h – GadTools menu management for Eg UTF-8 text editor.
 *
 * nm_UserData encoding: ACTION_* ids are stored in the upper 16 bits
 * (via ACTION_UD macro).  MSG_* locale ids for branch/title items are
 * stored in the lower 16 bits with upper bits clear.
 */

#ifndef EGMENU_H
#define EGMENU_H

#include <exec/types.h>
#include <intuition/intuition.h>
#include "egaction.h"
#include "emojigearsettings.h"
/* Menu state */
typedef struct EgMenu {
    struct Menu *menu;
    APTR         visualInfo;
} EgMenu;

/* Create menus and attach to window.  Returns TRUE on success. */
BOOL EgMenu_Create(EgMenu *bm, struct Screen *screen, struct Window *window, AppSettings *settings);

/* Remove menus from window and free all resources. */
void EgMenu_Close(EgMenu *bm, struct Window *window);

/* Close and recreate menus (e.g. after recent-files list changes). */
BOOL EgMenu_Rebuild(EgMenu *bm, struct Screen *screen, struct Window *window, AppSettings *settings);

/* Translate a menu selection code (WMHI_MENUPICK result & WMHI_MENUMASK,
 * or imsg->Code for IDCMP_MENUPICK) to an ACTION_* id.
 * Returns -1 if no valid selection. */
LONG EgMenu_ToActionID(EgMenu *bm, UWORD menuCode);

/* Find the MenuItem for a given ACTION_* id (walk all menus/items).
 * Returns NULL if not found. Use item->Flags & CHECKED to read tick state. */
struct MenuItem *EgMenu_FindItemByActionID(EgMenu *bm, ULONG actionID);

#endif /* EGMENU_H */
