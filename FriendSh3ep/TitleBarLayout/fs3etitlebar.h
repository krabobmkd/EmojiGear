#ifndef FS3ETITLEBAR_H
#define FS3ETITLEBAR_H

/*
 * TitleBarLayout - layout.gadget subclass for Part A of the FriendSh3ep
 * main window.  Always two rows of height dpiHeight each:
 *
 *   Row 1: [X]  ...drag area...  [-][=][^]
 *   Row 2: [icon] [posts label ] [new-posts label]
 *
 * Children MUST be added via LAYOUT_AddChild in this exact order:
 *   0  close button
 *   1  iconify button
 *   2  altpos button
 *   3  depth button
 *   4  user icon gadget  (placeholder)
 *   5  post-count label
 *   6  new-post-count label
 */

#include <exec/types.h>
#include <intuition/classusr.h>

#define TBLAYOUT_NUMCHILDREN  7

#define TBLAYOUT_Base         (TAG_USER | 0x53510UL)
#define TBLAYOUT_DpiHeight    (TBLAYOUT_Base + 0)  /* UWORD: row height in pixels */

extern Class *TitleBarLayoutClass;

int  TitleBarLayout_Init(void);
void TitleBarLayout_Exit(void);

#endif /* FS3ETITLEBAR_H */
