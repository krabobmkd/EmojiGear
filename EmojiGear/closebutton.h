#ifndef CLOSEBUTTON_H
#define CLOSEBUTTON_H

#include <exec/types.h>
#include <intuition/intuition.h>

/*
 * Tab close-button image (CLICKTAB_CloseImage).
 *
 * Builds a small 8x8 rectangular button with a bluish face, as a
 * penmap.image remapped to the given screen's palette.  Pen 0 is mapped
 * to the screen/window background
 * (PENMAP_Transparent), so the button blends into the tab bar.  The image
 * carries both a normal and a "selected" (pressed/hover) rendering, so the
 * same object can be passed directly as CLICKTAB_CloseImage.
 *
 * Returns NULL if images/penmap.image isn't open or scr is NULL.
 */
struct Image *CloseButton_CreateImage(struct Screen *scr);

#endif /* CLOSEBUTTON_H */
