/*
 * unibuttonp9.h – public API for the UniButtonP9 private BOOPSI gadget.
 *
 * Fork of UniButtonGadget with prefix UBTP9_ instead of UBT_.
 * Statically linked, private class (no AddClass/RemoveClass).
 * Requires URPBase (utf8rastport.library v4) opened by the caller.
 * BevelBase (images/bevel.image v32) is optional.
 */

#ifndef UNIBUTTONP9_H
#define UNIBUTTONP9_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/classes.h>
#include <utility/tagitem.h>

/* =========================================================================
 * Attribute tags   (base TAG_USER | 0x53540)
 * Access key:  I=OM_NEW  S=OM_SET  G=OM_GET
 * =========================================================================
 */
#define UBTP9_Dummy           (TAG_USER | 0x53540UL)

/* [IS] (ULONG) Font point size. Default: 14. */
#define UBTP9_PointSize       (UBTP9_Dummy + 1)

/* [IS] (ULONG) URP_PREF_* flags forwarded to the draw context. */
#define UBTP9_URPPrefs        (UBTP9_Dummy + 2)

/* [IS] (ULONG) Pen for text foreground. Default: 1. */
#define UBTP9_TextPen         (UBTP9_Dummy + 3)

/* [IS] (ULONG) Pen for button background (normal). Default: 0. */
#define UBTP9_BgPen           (UBTP9_Dummy + 4)

/* [IS] (ULONG) Pen for button background when selected. Default: FILLPEN (3). */
#define UBTP9_SelBgPen        (UBTP9_Dummy + 5)

/* [IS] (ULONG) URPFONT_* flags for subsequent UBTP9_AddFont calls. Default: 0. */
#define UBTP9_FontFlags       (UBTP9_Dummy + 6)

/* [IS] (STRPTR) Load a font into the draw context. */
#define UBTP9_AddFont         (UBTP9_Dummy + 7)

/* [IS] (any) Flush all fonts and the glyph cache. */
#define UBTP9_FlushFonts      (UBTP9_Dummy + 8)

/* [IG] (ULONG) BVS_* bevel style. Default: BVS_BUTTON. Set at OM_NEW only. */
#define UBTP9_BevelStyle      (UBTP9_Dummy + 9)

/* [IS] (BOOL) Skip background fill (transparent button). Default: FALSE. */
#define UBTP9_Transparent     (UBTP9_Dummy + 10)

/* [ISG] (struct URPDrawContext *) Shared draw context (retained via URPDC_Retain). */
#define UBTP9_URPDrawContext  (UBTP9_Dummy + 11)

/* [ISG] (ULONG) Left pixel margin inside bevel frame. Default: 4. */
#define UBTP9_LeftMargin      (UBTP9_Dummy + 12)

/* [ISG] (ULONG) Right pixel margin inside bevel frame. Default: 4. */
#define UBTP9_RightMargin     (UBTP9_Dummy + 13)

/* [ISG] (ULONG) Top pixel margin inside bevel frame. Default: 2. */
#define UBTP9_TopMargin       (UBTP9_Dummy + 14)

/* [ISG] (ULONG) Bottom pixel margin inside bevel frame. Default: 2. */
#define UBTP9_BottomMargin    (UBTP9_Dummy + 15)

/* debug */
#define UBTP9_FlushDebugOutput (UBTP9_Dummy + 16)

/* [ISG] (BOOL) Push/latch toggle mode. GA_Selected reflects latched state. */
#define UBTP9_PushButton       (UBTP9_Dummy + 17)

/* =========================================================================
 * Class management
 * =========================================================================
 */
extern Class *UniButtonP9Class;

int  UniButtonP9_Init(void);
void UniButtonP9_Exit(void);

#endif /* UNIBUTTONP9_H */
