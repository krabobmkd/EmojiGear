/*
 * unibuttonbgbm.h – public API for the UniButtonBGBM private BOOPSI gadget.
 *
 * Fork of UniButtonP9 with prefix UBGBM_ instead of UBTP9_, stripped of
 * Patch9 support. Two backgrounds per state, chosen per state at blit time:
 *
 *   - Default: a small offscreen bitmap caches the state's text (blended
 *     against a flat UBGBM_BgPen/UBGBM_SelBgPen fill), rebuilt in GM_RENDER
 *     (application-task context – FreeType calls safe) and blitted as-is
 *     from GM_GOACTIVE/GM_HANDLEINPUT/GM_GOINACTIVE (input.device context –
 *     no FreeType there).
 *   - UBGBM_Style set, and that state's image loaded: background is a
 *     full-opaque image (no transparency) and text both drawn live
 *     (no cache) -- see ubgbm_blit_state() in unibuttonbgbm_attribs.c for
 *     why (the image isn't flat, so a cached flat-bgPen text rectangle
 *     blitted on top would show as a visible mismatched box).
 *
 * See UniButtonP9 (unibuttonp9.h) for the sibling class that always uses a
 * Patch9 9-slice skin and always draws everything live.
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
#include <dos/dos.h>

/* Signalled at the owning task (see the callerTask comment in
 * unibuttonbgbm_private.h) from GM_RENDER whenever UBGBM_Patch9Mode's
 * gadget-sized cache is invalid (e.g. just resized) AND GM_RENDER is
 * running off that task -- rebuilding calls FreeType, unsafe there, so it
 * can only mark the cache invalid and ask the real owning task to redo a
 * GM_RENDER pass soon. The caller must Wait() on this bit (alongside its
 * own signals, same idiom as FS3ETIMER_SIGNAL in fs3etimer.h) and respond
 * by forcing a real, on-task redraw of every UniButtonBGBM gadget it owns
 * (e.g. RefreshGList() on their containing layout) -- see main()'s Wait()
 * loop in friendsh3ep.c. CTRL_C/D/F are already claimed by this app (see
 * main()), so this class claims CTRL_E. */
#define UBGBM_REFRESH_SIGNAL SIGBREAKF_CTRL_E

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

/* [IS] (FS3EStyle *) Optional theme: when set, and
 * style->btbgbmBitmap[state] loaded (see fs3estyle.h), that state's
 * background is a full-opaque (no transparency) image drawn live, and
 * text uses FS3E_COLOR_BTBGBM_TEXT_* instead of UBGBM_TextPen/BgPen --
 * see ubgbm_blit_state() in unibuttonbgbm_attribs.c. Any state whose
 * image failed to load (or with no style set at all) keeps the normal
 * cached flat-colour fill. Borrowed pointer: caller owns it; not copied. */
#define UBGBM_Style            (UBGBM_Dummy + 18)

/* [ISG] (WORD) X offset into the style-image background (UBGBM_Style)
 * source bitmap, e.g. for a background image made deliberately larger
 * than any button size, so different buttons/states can crop a different
 * region of the same shared source. Default: 0. Clamped at draw time so
 * bgShiftX + gadget width never exceeds the source bitmap's width (see
 * ubgbm_blit_state() in unibuttonbgbm_attribs.c) -- no effect in
 * flat-colour mode (no UBGBM_Style, or that state's image not loaded). */
#define UBGBM_BgShiftX         (UBGBM_Dummy + 19)

/* [ISG] (WORD) Same as UBGBM_BgShiftX, for the Y offset / gadget height. */
#define UBGBM_BgShiftY         (UBGBM_Dummy + 20)

/* [ISG] (BOOL) Selects the Patch9 background render mode: when set (and
 * UBGBM_Style is also set), each state's background is drawn from
 * style->patch9[FS3ESTYLE_PATCH9_BTBGBM] (see fs3estyle.h) instead of
 * style->btbgbmBitmap[state] -- opaque, no transparency mask (see
 * Patch9_DrawOpaque in patch9.h), so unlike UBGBM_Style's live-drawn image
 * mode this whole composited state (background + text) is cached like
 * flat-colour mode is. No bevel is drawn in this mode -- the button border
 * is expected to live in the Patch9 image itself. Mutually exclusive with
 * plain UBGBM_Style image mode: when set, the Patch9 skin always takes
 * priority. Default: FALSE. See unibuttonbgbm_render.c. */
#define UBGBM_Patch9Mode       (UBGBM_Dummy + 21)

#define UBGBM_RefreshNeeded (UBGBM_Dummy + 22)

/* =========================================================================
 * Class management
 * =========================================================================
 */
extern Class *UniButtonBGBMClass;

int  UniButtonBGBM_Init(void);
void UniButtonBGBM_Exit(void);

#endif /* UNIBUTTONBGBM_H */
