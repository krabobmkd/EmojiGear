#ifndef FS3ETITLEBAR_H
#define FS3ETITLEBAR_H

/*
 * TitleBarLayout - layout.gadget subclass for Part A of the FriendSh3ep
 * main window.  Always two rows of height dpiHeight each:
 *
 *   Row 1: [X]  ...drag area...  [-][=][^]
 *   Row 2: [icon]  ...  [settings][accounts][toot+]
 *
 * Children MUST be added via LAYOUT_AddChild in this exact order:
 *   0  close button
 *   1  iconify button
 *   2  altpos button
 *   3  depth button
 *   4  settings button
 *   5  accounts button
 *   6  new-toot button
 *
 * The row-2 user icon is NOT a child gadget: it's drawn directly by
 * TitleBarLayout_OnRender() from TBLAYOUT_AvatarImages/TBLAYOUT_AccountAcct,
 * the same way TootTimeline draws toot avatars (RgbImage_DrawScaled) --
 * see the file header comment in fs3etitlebar.c for why a button.gadget
 * wrapping an images/bitmap.image was dropped.
 */

#include <exec/types.h>
#include <intuition/classusr.h>
#include "../fs3estyle.h"

#define TBLAYOUT_NUMCHILDREN  7

#define TBLAYOUT_Base         (TAG_USER | 0x53510UL)
#define TBLAYOUT_DpiHeight    (TBLAYOUT_Base + 0)  /* UWORD: row height in pixels */
#define TBLAYOUT_Style        (TBLAYOUT_Base + 1)  /* FS3EStyle *: layout metrics + colors */
/* [IS] struct AvatarImages*: avatar bitmap cache; gadget reads from it
 * during render, does not own it (same convention as
 * TTIMELINE_AvatarImages). */
#define TBLAYOUT_AvatarImages (TBLAYOUT_Base + 2)
/* [IS] STRPTR: connected account's @user@instance, used as the
 * AvatarImages cache key for the row-2 user icon. Gadget copies the
 * string (unlike AvatarImages, this can legitimately change/clear across
 * a re-login). NULL/"" draws no icon. */
#define TBLAYOUT_AccountAcct  (TBLAYOUT_Base + 3)

extern Class *TitleBarLayoutClass;

int  TitleBarLayout_Init(void);
void TitleBarLayout_Exit(void);

#endif /* FS3ETITLEBAR_H */
