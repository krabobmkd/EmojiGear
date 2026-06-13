#ifndef BORDERSCROLLERS_H_
#define BORDERSCROLLERS_H_

#include <intuition/intuition.h>
#include <intuition/screens.h>

/**
 * Manage gadtools-level border scrollers
 * inside intuition window borders.
 * This is by intuition struct window instance, like OS3 menu.
 */

typedef struct sGtBorderScroll{

    /* Cached size-gadget dimensions (used for prop gadget layout) */
    LONG              sizeW;
    LONG              sizeH;

    /* Border scroller gadgets (PROPGCLASS) */
    struct Gadget    *yprop;        /* vertical, right border   */
    struct Gadget    *xprop;        /* horizontal, bottom border */

    /* Border arrow buttons (BUTTONGCLASS) */
    struct Gadget    *dArrowBtn;    /* down  – head of WA_Gadgets chain */
    struct Gadget    *uArrowBtn;
    struct Gadget    *rArrowBtn;
    struct Gadget    *lArrowBtn;

    /* SYSICLASS images (must outlive gadgets; freed after CloseWindow) */
    struct Image     *sizeImg;
    struct Image     *uArrowImg;
    struct Image     *dArrowImg;
    struct Image     *lArrowImg;
    struct Image     *rArrowImg;

    int     scrollersAttached;

} sGtBorderScroll;

/* this allocates a linked list of border gadget instances, but does not attach them.
 * This must be AddGadgets() attached to a window at init or after (to be verified)
 * with like of: WA_Gadgets,(ULONG)p->dArrowBtn
*/
int BorderScroll_Init(sGtBorderScroll *borderScroll,  struct DrawInfo  *drawInfo, struct Screen	*screen);

/* Does unallocated, as it's gadtools attached, dispose is to be done after CloseWindow */
void BorderScroll_Exit(sGtBorderScroll *borderScroll);

#endif

