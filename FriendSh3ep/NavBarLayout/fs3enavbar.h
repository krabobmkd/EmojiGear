#ifndef FS3ENAVBAR_H
#define FS3ENAVBAR_H

/*
 * NavBarLayout - layout.gadget subclass for Part B of the FriendSh3ep
 * main window.  Holds exactly 8 buttons.
 *
 * Adaptive mode (decided at GM_LAYOUT time from available width). The
 * overall layout height is ALWAYS room for 2 button rows, regardless of
 * mode, so it stays stable across row-mode flips (window resize, font
 * size change, ...):
 *   single row  — when width >= 8 * minButtonWidth
 *                 all 8 buttons fill one line, each getting the full
 *                 2-row height (i.e. twice their minimal height)
 *   double row  — otherwise
 *                 4 buttons per row × 2 rows, each row gets one
 *                 button's minimal height
 *
 * Children MUST be added via LAYOUT_AddChild in this exact order:
 *   0  Home
 *   1  Notifications
 *   2  Local
 *   3  Federated
 *   4  New Toot
 *   5  Search
 *   6  Settings
 *   7  Accounts
 */

#include <exec/types.h>
#include <intuition/classusr.h>

#define NBLAYOUT_NUMBUTTONS  8

#define NBLAYOUT_Base        (TAG_USER | 0x53520UL)
#define NBLAYOUT_DpiHeight   (NBLAYOUT_Base + 0)  /* UWORD: row height in pixels */

extern Class *NavBarLayoutClass;

int  NavBarLayout_Init(void);
void NavBarLayout_Exit(void);

#endif /* FS3ENAVBAR_H */
