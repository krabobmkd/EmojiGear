/*
 * unibuttonbgbm.h – public API for the UniButtonBGBM private BOOPSI gadget.
 *
 * Fork of UniButtonP9 with prefix UBGBM_ instead of UBTP9_, stripped of
 * Patch9 support: this class always caches each state (normal/selected/
 * disabled) as a small offscreen bitmap (text only, blended against a flat
 * UBGBM_BgPen/UBGBM_SelBgPen fill), rebuilt in GM_RENDER (application-task
 * context – FreeType calls safe) and blitted as-is from GM_GOACTIVE/
 * GM_HANDLEINPUT/GM_GOINACTIVE (input.device context – no FreeType there).
 * See UniButtonP9 (unibuttonp9.h) for the sibling class that always uses a
 * Patch9 9-slice skin and draws everything live instead.
 *
 * Statically linked, private class (no AddClass/RemoveClass).
 * Requires URPBase (utf8rastport.library v4) opened by the caller.
 * BevelBase (images/bevel.image v32) is optional.
 */

#ifndef UNIBUTTONBGBM_H
#define UNIBUTTONBGBM_H

#include <exec/types.h>
#include <intuition/classusr.h>
#include <intuition/gadgetclass.h>
#include <intuition/classes.h>
#include <utility/tagitem.h>

/* =========================================================================
 * Attribute tags   (base TAG_USER | 0x53550)
 * Access key:  I=OM_NEW  S=OM_SET  G=OM_GET
 * =========================================================================
 */
#define UBGBM_Dummy           (TAG_USER | 0x53550UL)

/* [IS] (ULONG) Font point size. Default: 14. */
#define UBGBM_PointSize       (UBGBM_Dummy + 1)

/* [IS] (ULONG) URP_PREF_* flags forwarded to the draw context. */
#define UBGBM_URPPrefs        (UBGBM_Dummy + 2)

/* [IS] (ULONG) Pen for text foreground. Default: 1. */
#define UBGBM_TextPen         (UBGBM_Dummy + 3)

/* [IS] (ULONG) Pen for button background (normal). Default: 0. */
#define UBGBM_BgPen           (UBGBM_Dummy + 4)

/* [IS] (ULONG) Pen for button background when selected. Default: FILLPEN (3). */
#define UBGBM_SelBgPen        (UBGBM_Dummy + 5)

/* [IS] (ULONG) URPFONT_* flags for subsequent UBGBM_AddFont calls. Default: 0. */
#define UBGBM_FontFlags       (UBGBM_Dummy + 6)

/* [IS] (STRPTR) Load a font into the draw context. */
#define UBGBM_AddFont         (UBGBM_Dummy + 7)

/* [IS] (any) Flush all fonts and the glyph cache. */
#define UBGBM_FlushFonts      (UBGBM_Dummy + 8)

/* [IG] (ULONG) BVS_* bevel style. Default: BVS_BUTTON. Set at OM_NEW only. */
#define UBGBM_BevelStyle      (UBGBM_Dummy + 9)

/* [IS] (BOOL) Skip background fill (transparent button). Default: FALSE. */
#define UBGBM_Transparent     (UBGBM_Dummy + 10)

/* [ISG] (struct URPDrawContext *) Shared draw context (retained via URPDC_Retain). */
#define UBGBM_URPDrawContext  (UBGBM_Dummy + 11)

/* [ISG] (ULONG) Left pixel margin inside bevel frame. Default: 4. */
#define UBGBM_LeftMargin      (UBGBM_Dummy + 12)

/* [ISG] (ULONG) Right pixel margin inside bevel frame. Default: 4. */
#define UBGBM_RightMargin     (UBGBM_Dummy + 13)

/* [ISG] (ULONG) Top pixel margin inside bevel frame. Default: 2. */
#define UBGBM_TopMargin       (UBGBM_Dummy + 14)

/* [ISG] (ULONG) Bottom pixel margin inside bevel frame. Default: 2. */
#define UBGBM_BottomMargin    (UBGBM_Dummy + 15)

/* debug */
#define UBGBM_FlushDebugOutput (UBGBM_Dummy + 16)

/* [ISG] (BOOL) Push/latch toggle mode. GA_Selected reflects latched state. */
#define UBGBM_PushButton       (UBGBM_Dummy + 17)

/* =========================================================================
 * Class management
 * =========================================================================
 */
extern Class *UniButtonBGBMClass;

int  UniButtonBGBM_Init(void);
void UniButtonBGBM_Exit(void);

#endif /* UNIBUTTONBGBM_H */
