/*
 * unibutton.h – public API for the UniButton BOOPSI gadget
 *
 * A push-button gadget that renders its label using the utf8rastport
 * FreeType engine, enabling full Unicode / Emoji text in the label.
 *
 * The visual for normal/selected/disabled states is pre-rendered into
 * cached OffscreenBitMaps so that responding to mouse clicks (which
 * runs in input.device context) only needs a safe BltBitMapRastPort.
 *
 * Margins (UBT_*Margin) are measured INSIDE the bevel frame.
 * The bevel thickness is added automatically on top.
 *
 * A URPDrawContext (font+glyph cache) can be shared across multiple
 * UniButton instances via UBT_URPDrawContext, exactly like
 * UTED_URPDrawContext in UniTextEditor.
 *
 * Inherits from "gadgetclass".
 */

#ifndef UNIBUTTON_H
#define UNIBUTTON_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/classes.h>
#include <utility/tagitem.h>

/* =========================================================================
 * Attribute tags   (base TAG_USER | 0x7300)
 *
 * Access key:  I=OM_NEW  S=OM_SET  G=OM_GET
 * =========================================================================
 */
#define UBT_Dummy           (TAG_USER | 0x7300)

/* [IS] (ULONG) Font point size. Default: 14. */
#define UBT_PointSize       (UBT_Dummy + 1)

/* [IS] (ULONG) URP_PREF_* flags forwarded to the draw context.
 *              Default: URP_PREF_ANTIALIAS | URP_PREF_CLUTMODE_NOMASK | URP_PREF_HIGHFILTERING. */
#define UBT_URPPrefs        (UBT_Dummy + 2)

/* [IS] (ULONG) Intuition pen for text foreground. Default: 1. */
#define UBT_TextPen         (UBT_Dummy + 3)

/* [IS] (ULONG) Intuition pen for button background (normal/unselected). Default: 0. */
#define UBT_BgPen           (UBT_Dummy + 4)

/* [IS] (ULONG) Intuition pen for button background when pressed/selected.
 *              Default: FILLPEN from DrawInfo (pen 3). */
#define UBT_SelBgPen        (UBT_Dummy + 5)

/* [IS] (ULONG) URPFONT_* flags stored for subsequent UBT_AddFont calls.
 *              Default: 0. */
#define UBT_FontFlags       (UBT_Dummy + 6)

/* [IS] (STRPTR) Load a font into the draw context using the current
 *               UBT_PointSize and UBT_FontFlags. Not copied by the gadget. */
#define UBT_AddFont         (UBT_Dummy + 7)

/* [IS] (any) Flush all fonts and the glyph cache. */
#define UBT_FlushFonts      (UBT_Dummy + 8)

/* [IG] (ULONG) BVS_* bevel style for the button border.
 *              Default: BVS_BUTTON.  Set at OM_NEW only. */
#define UBT_BevelStyle      (UBT_Dummy + 9)

/* [IS] (BOOL) When TRUE, background fill is skipped (transparent button).
 *             Default: FALSE. */
#define UBT_Transparent     (UBT_Dummy + 10)

/* [ISG] (struct URPDrawContext *) Shared font/glyph draw context.
 *
 * When provided at OM_NEW, the gadget retains it (URPDC_Retain) instead of
 * creating its own.  Lets multiple UniButton instances share the same font
 * configuration and glyph cache, exactly like UTED_URPDrawContext. */
#define UBT_URPDrawContext  (UBT_Dummy + 11)

/* [ISG] (ULONG) Left pixel margin inside the bevel frame. Default: 4. */
#define UBT_LeftMargin      (UBT_Dummy + 12)

/* [ISG] (ULONG) Right pixel margin inside the bevel frame. Default: 4. */
#define UBT_RightMargin     (UBT_Dummy + 13)

/* [ISG] (ULONG) Top pixel margin inside the bevel frame. Default: 2. */
#define UBT_TopMargin       (UBT_Dummy + 14)

/* [ISG] (ULONG) Bottom pixel margin inside the bevel frame. Default: 2. */
#define UBT_BottomMargin    (UBT_Dummy + 15)

/* debug purpose */
#define UBT_FlushDebugOutput    (UBT_Dummy + 16)

/* [ISG] (BOOL) When TRUE, button latches like BUTTON_PushButton: each click
 *              toggles the selected/depressed state.  GA_Selected reflects the
 *              latched state.  Default: FALSE. */
#define UBT_PushButton          (UBT_Dummy + 17)



/* =========================================================================
 * Notification attribute tags
 *
 * Sent via OM_NOTIFY when the button is released while the pointer is
 * still over the gadget.  Connect to an ICA_TARGET with ICA_MAP.
 * =========================================================================
 */
#define UBTN_Dummy          (TAG_USER | 0x73C0)

/* Button was activated.  Value = GA_ID of the button.
 Correction, useless ,GA_Selected TRUE is notified
#define UBTN_Pressed        (UBTN_Dummy + 1)
*/

/* =========================================================================
 * Class management  (static-link mode)
 * =========================================================================
 */
#ifdef USE_STATIC_UNIBUTTON
    extern Class *UniButtonClass;
    int  UniButton_Init(void);
    void UniButton_Exit(void);
#endif

#endif /* UNIBUTTON_H */
