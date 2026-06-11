#include "borderscrollers.h"

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utility.h>

#include <intuition/classusr.h>
#include <intuition/imageclass.h>
#include <intuition/icclass.h>
#include <intuition/gadgetclass.h>
#include <intuition/screens.h>

#include "gadgetid.h"

#include <stdio.h>

extern struct Window *CurrentMainWindow;
extern Object       *TargetInstance;

int BorderScroll_Init(sGtBorderScroll *borderScroll,  struct DrawInfo  *drawInfo, struct Screen	*screen)
{
    LONG arrowW = 0, arrowH = 0;
    LONG barh;
// printf("BorderScroll_Init\n");

    /* ---- Size-corner image ---- */
    borderScroll->sizeImg = (struct Image *)NewObject(NULL, SYSICLASS,
        SYSIA_Which,    SIZEIMAGE,
        SYSIA_Size,     SYSISIZE_MEDRES,
        SYSIA_DrawInfo, drawInfo,
        TAG_END);
    if (borderScroll->sizeImg) {
        GetAttr(IA_Width,  borderScroll->sizeImg, (ULONG *)&borderScroll->sizeW);
        GetAttr(IA_Height, borderScroll->sizeImg, (ULONG *)&borderScroll->sizeH);
    } else {
        borderScroll->sizeW = 18; borderScroll->sizeH = 10;
    }

    /* ---- Down-arrow image + button (right border, above size corner) ---- */
    borderScroll->dArrowImg = (struct Image *)NewObject(NULL, SYSICLASS,
        SYSIA_Which,    DOWNIMAGE,
        SYSIA_Size,     SYSISIZE_MEDRES,
        SYSIA_DrawInfo, drawInfo,
        TAG_END);
    if (borderScroll->dArrowImg) {
        GetAttr(IA_Width,  borderScroll->dArrowImg, (ULONG *)&arrowW);
        GetAttr(IA_Height, borderScroll->dArrowImg, (ULONG *)&arrowH);
    } else { arrowW = 18; arrowH = 11; }

    borderScroll->dArrowBtn = (struct Gadget *)NewObject(NULL, BUTTONGCLASS,
        GA_ID,          GID_DARROW,
        GA_RelRight,    -arrowW + 1,
        GA_RelBottom,   -(LONG)borderScroll->sizeH - arrowH + 1,
        GA_Width,       arrowW,
        GA_Height,      arrowH,
        GA_DrawInfo,    drawInfo,
     //   GA_GZZGadget,   TRUE,
        GA_RightBorder, TRUE,
        GA_Image,       borderScroll->dArrowImg,
        ICA_TARGET,     (ULONG)TargetInstance,
        TAG_END);
    // printf("borderScroll->dArrowBtn:%08x\n",(int)borderScroll->dArrowBtn);

     if (!borderScroll->dArrowBtn) goto fail;


    /* ---- Up-arrow image + button (right border, above down arrow) ---- */
    borderScroll->uArrowImg = (struct Image *)NewObject(NULL, SYSICLASS,
        SYSIA_Which,    UPIMAGE,
        SYSIA_Size,     SYSISIZE_MEDRES,
        SYSIA_DrawInfo, drawInfo,
        TAG_END);
    /* reuse arrowW/arrowH – up and down arrows share the same size */

    borderScroll->uArrowBtn = (struct Gadget *)NewObject(NULL, BUTTONGCLASS,
        GA_Previous,    borderScroll->dArrowBtn,
        GA_ID,          GID_UARROW,
        GA_RelRight,    -arrowW + 1,
        GA_RelBottom,   -(LONG)borderScroll->sizeH - arrowH - arrowH + 1,
        GA_Width,       arrowW,
        GA_Height,      arrowH,
        GA_DrawInfo,    drawInfo,
        //GA_GZZGadget,   TRUE,
        GA_RightBorder, TRUE,
        GA_Image,       borderScroll->uArrowImg,
        ICA_TARGET,     (ULONG)TargetInstance,
        TAG_END);
    if (!borderScroll->uArrowBtn) goto fail;

    /* ---- Vertical prop gadget (right border, between title bar and arrows) ---- */
    barh = (LONG)screen->WBorTop + (LONG)screen->RastPort.TxHeight + 2L;

    borderScroll->yprop = (struct Gadget *)NewObject(NULL, PROPGCLASS,
        GA_Previous,    borderScroll->uArrowBtn,
        GA_ID,          GID_YPROP,
        GA_RelRight,    -(LONG)borderScroll->sizeW + 4,
        GA_Top,         barh,
        GA_Width,       (LONG)borderScroll->sizeW - 6,
        GA_RelHeight,   -(LONG)borderScroll->sizeH - arrowH - arrowH - barh - 1,
        GA_DrawInfo,    drawInfo,
       // GA_GZZGadget,   TRUE,
        GA_RightBorder, TRUE,
        PGA_Freedom,    FREEVERT,
        PGA_Borderless, TRUE,
        PGA_NewLook,    TRUE,
        PGA_Total,      1,
        PGA_Visible,    1,
        PGA_Top,        0,
        ICA_TARGET,     (ULONG)TargetInstance,
        TAG_END);
    if (!borderScroll->yprop) goto fail;

    /* ---- Right-arrow image + button (bottom border) ---- */
    borderScroll->rArrowImg = (struct Image *)NewObject(NULL, SYSICLASS,
        SYSIA_Which,    RIGHTIMAGE,
        SYSIA_Size,     SYSISIZE_MEDRES,
        SYSIA_DrawInfo, drawInfo,
        TAG_END);
    {
        LONG rw = arrowW, rh = arrowH;
        if (borderScroll->rArrowImg) {
            GetAttr(IA_Width,  borderScroll->rArrowImg, (ULONG *)&rw);
            GetAttr(IA_Height, borderScroll->rArrowImg, (ULONG *)&rh);
        }

        borderScroll->rArrowBtn = (struct Gadget *)NewObject(NULL, BUTTONGCLASS,
            GA_Previous,      borderScroll->yprop,
            GA_ID,            GID_RARROW,
            GA_RelRight,      -(LONG)borderScroll->sizeW - rw + 1,
            GA_RelBottom,     -rh + 1,
            GA_Width,         rw,
            GA_Height,        rh,
            GA_DrawInfo,      drawInfo,
        //    GA_GZZGadget,     TRUE,
            GA_BottomBorder,  TRUE,
            GA_Image,         borderScroll->rArrowImg,
            ICA_TARGET,       (ULONG)TargetInstance,
            TAG_END);
        if (!borderScroll->rArrowBtn) goto fail;
    }

    /* ---- Left-arrow image + button (bottom border) ---- */
    borderScroll->lArrowImg = (struct Image *)NewObject(NULL, SYSICLASS,
        SYSIA_Which,    LEFTIMAGE,
        SYSIA_Size,     SYSISIZE_MEDRES,
        SYSIA_DrawInfo, drawInfo,
        TAG_END);
    {
        LONG lw = arrowW, lh = arrowH;
        LONG rw = arrowW;
        if (borderScroll->lArrowImg) {
            GetAttr(IA_Width,  borderScroll->lArrowImg, (ULONG *)&lw);
            GetAttr(IA_Height, borderScroll->lArrowImg, (ULONG *)&lh);
        }
        if (borderScroll->rArrowBtn)
            GetAttr(GA_Width, borderScroll->rArrowBtn, (ULONG *)&rw);

        borderScroll->lArrowBtn = (struct Gadget *)NewObject(NULL, BUTTONGCLASS,
            GA_Previous,      borderScroll->rArrowBtn,
            GA_ID,            GID_LARROW,
            GA_RelRight,      -(LONG)borderScroll->sizeW - rw - lw + 1,
            GA_RelBottom,     -lh + 1,
            GA_Width,         lw,
            GA_Height,        lh,
            GA_DrawInfo,      drawInfo,
         //   GA_GZZGadget,     TRUE,
            GA_BottomBorder,  TRUE,
            GA_Image,         borderScroll->lArrowImg,
            ICA_TARGET,       (ULONG)TargetInstance,
            TAG_END);
        if (!borderScroll->lArrowBtn) goto fail;
    }

    /* ---- Horizontal prop gadget (bottom border) ---- */
    {
        LONG rw = arrowW, lw = arrowW;
        if (borderScroll->rArrowBtn) GetAttr(GA_Width, borderScroll->rArrowBtn, (ULONG *)&rw);
        if (borderScroll->lArrowBtn) GetAttr(GA_Width, borderScroll->lArrowBtn, (ULONG *)&lw);
        {
            LONG ph = arrowH;
            if (borderScroll->lArrowBtn) GetAttr(GA_Height, borderScroll->lArrowBtn, (ULONG *)&ph);

            borderScroll->xprop = (struct Gadget *)NewObject(NULL, PROPGCLASS,
                GA_Previous,      borderScroll->lArrowBtn,
                GA_ID,            GID_XPROP,
                GA_Left,          (LONG)screen->WBorLeft,
                GA_RelBottom,     -(LONG)borderScroll->sizeH + 3,
                GA_RelWidth,      -(LONG)borderScroll->sizeW - rw - lw
                                  - (LONG)screen->WBorLeft - 1,
                GA_Height,        (LONG)borderScroll->sizeH - 4,
                GA_DrawInfo,      drawInfo,
             //   GA_GZZGadget,     TRUE,
                GA_BottomBorder,  TRUE,
                PGA_Freedom,      FREEHORIZ,
                PGA_Borderless,   TRUE,
                PGA_NewLook,      TRUE,
                PGA_Total,        1,
                PGA_Visible,      1,
                PGA_Top,          0,
                ICA_TARGET,       (ULONG)TargetInstance,
                TAG_END);
        }
    }
    if (!borderScroll->xprop) goto fail;


    return 1;
fail:
    return 0;

}
void BorderScroll_Exit(sGtBorderScroll *borderScroll)
{
// printf("BorderScroll_Exit\n");
    /* Border gadgets: dispose after CloseWindow */
    if (borderScroll->xprop)     { DisposeObject(borderScroll->xprop);     borderScroll->xprop     = NULL; }
    if (borderScroll->lArrowBtn) { DisposeObject(borderScroll->lArrowBtn); borderScroll->lArrowBtn = NULL; }
    if (borderScroll->lArrowImg) { DisposeObject(borderScroll->lArrowImg); borderScroll->lArrowImg = NULL; }
    if (borderScroll->rArrowBtn) { DisposeObject(borderScroll->rArrowBtn); borderScroll->rArrowBtn = NULL; }
    if (borderScroll->rArrowImg) { DisposeObject(borderScroll->rArrowImg); borderScroll->rArrowImg = NULL; }
    if (borderScroll->yprop)     { DisposeObject(borderScroll->yprop);     borderScroll->yprop     = NULL; }
    if (borderScroll->uArrowBtn) { DisposeObject(borderScroll->uArrowBtn); borderScroll->uArrowBtn = NULL; }
    if (borderScroll->uArrowImg) { DisposeObject(borderScroll->uArrowImg); borderScroll->uArrowImg = NULL; }
    if (borderScroll->dArrowBtn) { DisposeObject(borderScroll->dArrowBtn); borderScroll->dArrowBtn = NULL; }
    if (borderScroll->dArrowImg) { DisposeObject(borderScroll->dArrowImg); borderScroll->dArrowImg = NULL; }
    if (borderScroll->sizeImg)   { DisposeObject(borderScroll->sizeImg);   borderScroll->sizeImg   = NULL; }

}
