/*
 * testoffscreenbm.c  –  isolate the OffscreenBitMap Init/Close cycle,
 *                        then draw UTF-8 text into it via utf8rastport.library.
 *
 * Links directly against UniButtonGadget/offscreenbm.c (no gadget library).
 * Sequence:
 *   1. Create an offscreen RastPort and immediately destroy it (no drawing).
 *      Proves DeleteLayer() alone does not corrupt the system.
 *   2. Create a second offscreen RastPort, load a TrueType font via
 *      utf8rastport.library, draw a UTF-8 / emoji string into it, then
 *      destroy it.  Proves the full render path is clean.
 *   3. Open a tiny Intuition window and wait for a close-gadget click to
 *      confirm the system is still sane.
 *
 * Usage:
 *   testoffscreenbm [font.ttf] [pointsize] [width] [height] [depth]
 *
 * Defaults: LiberationSans-Regular.ttf  16  200  50  8
 *
 * Written in C89 style for maximum Amiga compiler compatibility.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>
#include <graphics/layers.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/layers.h>
#include <proto/intuition.h>
#include <proto/utf8rastport.h>

#include <libraries/utf8rastport.h>

#include <stdio.h>
#include <stdlib.h>

/* offscreenbm.c is compiled directly into this test – no library needed. */
#include "../../UniButtonGadget/offscreenbm.h"

/* -------------------------------------------------------------------------
 * Global library bases required by proto/ inlines
 * ------------------------------------------------------------------------- */
struct GfxBase       *GfxBase       = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library       *LayersBase    = NULL;
struct Library       *URPBase       = NULL;

/* UTF-8 string with emoji: smiley + text + frog (same as testgadget) */
static const char *TEST_TEXT =
    "\xF0\x9F\x98\x8A Hello, offscreen! \xF0\x9F\x90\xB8";

/* -------------------------------------------------------------------------
 * test_create_close_only
 *
 * Phase 1: create + close with no drawing at all.
 * ------------------------------------------------------------------------- */
static int test_create_close_only(int width, int height, int depth)
{
    OffscreenBitMap ofsbm;

    printf("\n--- Phase 1: OffscreenBitMap_Init(%d x %d x %d), no drawing ---\n",
           width, height, depth);

    OffscreenBitMap_Init(&ofsbm, width, height, depth, 0, NULL);

    if (!ofsbm._bm)
        puts("  _bm        : FAILED (AllocBitMap)");
    else
        printf("  _bm        : OK  %p\n", (void *)ofsbm._bm);

    if (!ofsbm._rp)
        puts("  _rp        : FAILED (AllocVec/InitRastPort)");
    else
        printf("  _rp        : OK  %p\n", (void *)ofsbm._rp);

    if (!ofsbm._layerinfo)
        puts("  _layerinfo : FAILED (NewLayerInfo)");
    else
        printf("  _layerinfo : OK  %p\n", (void *)ofsbm._layerinfo);

    if (!ofsbm._layer)
        puts("  _layer     : FAILED (CreateUpfrontLayer)");
    else
        printf("  _layer     : OK  %p\n", (void *)ofsbm._layer);

    if (!ofsbm._bm || !ofsbm._rp) {
        puts("ERROR: offscreen bitmap creation failed.");
        OffscreenBitMap_Close(&ofsbm);
        return 0;
    }

    puts("  OffscreenBitMap_Close() ...");
    OffscreenBitMap_Close(&ofsbm);
    puts("  Close returned.");

    if (ofsbm._bm || ofsbm._rp || ofsbm._layerinfo || ofsbm._layer)
        puts("  WARNING: Close left non-NULL pointer(s).");
    else
        puts("  All pointers NULL: OK");

    return 1;
}

/* -------------------------------------------------------------------------
 * test_draw_text
 *
 * Phase 2: create offscreen bitmap, draw UTF-8 text, close.
 * ------------------------------------------------------------------------- */
static int test_draw_text(const char *fontPath, int pointSize,
                          int width, int height, int depth)
{
    OffscreenBitMap          ofsbm;
    struct URPDrawContext    *dc   = NULL;
    struct URPTextMetric     metric;
    struct URPTextPos        pos;
    int ok = 1;

    printf("\n--- Phase 2: draw \"%s\" into %dx%dx%d offscreen bitmap ---\n",
           fontPath, width, height, depth);

    /* Create draw context */
    dc = URPDC_Create(NULL);
    if (!dc) {
        puts("ERROR: URPDC_Create() returned NULL");
        return 0;
    }
    printf("  URPDC_Create    : OK  %p\n", (void *)dc);

    if (!URPDC_AddFont(dc, fontPath, (LONG)pointSize, 0L)) {
        printf("  URPDC_AddFont(%s, %d) : FAILED\n", fontPath, pointSize);
        ok = 0; goto done;
    }
    printf("  URPDC_AddFont(%s, %d) : OK\n", fontPath, pointSize);

    /* Measure text so we can report it */
    URPDC_TextSizeUTF8(dc, TEST_TEXT, -1, &metric);
    printf("  TextSize: width=%d height=%d ascent=%d\n",
           (int)metric.width, (int)metric.height, (int)metric.baseY);

    /* Create offscreen bitmap */
    OffscreenBitMap_Init(&ofsbm, width, height, depth, BMF_CLEAR, NULL);
    if (!ofsbm._rp) {
        puts("ERROR: offscreen bitmap creation failed.");
        ok = 0; goto done;
    }
    printf("  OffscreenBitMap_Init: OK  _rp=%p\n", (void *)ofsbm._rp);

    /* White background, black text (plain RGB – no screen needed) */
    SetAPen(ofsbm._rp, 0L);
    SetBPen(ofsbm._rp, 1L);
    SetDrMd(ofsbm._rp, JAM2);
    URPDC_SetDrawColor(dc, 0x00000000UL, 0x00FFFFFFUL);

    /* Draw text at baseline y = ascent, left-aligned */
    pos.x = 2;
    pos.y = (metric.baseY > 0) ? metric.baseY : (WORD)pointSize;

    puts("  URPDrawTextUTF8() ...");
    URPDrawTextUTF8(ofsbm._rp, dc, &pos, TEST_TEXT, (ULONG)(-1));
    puts("  URPDrawTextUTF8() returned.");

    puts("  OffscreenBitMap_Close() ...");
    OffscreenBitMap_Close(&ofsbm);
    puts("  Close returned.");

    if (ofsbm._bm || ofsbm._rp || ofsbm._layerinfo || ofsbm._layer)
        puts("  WARNING: Close left non-NULL pointer(s).");
    else
        puts("  All pointers NULL after Close: OK");

done:
    if (dc) URPDC_Release(dc);
    return ok;
}

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    const char *fontPath  = "LiberationSans-Regular.ttf";
    int         pointSize = 16;
    int         width     = 200;
    int         height    =  50;
    int         depth     =   8;
    struct Window       *win = NULL;
    struct IntuiMessage *imsg;
    int ok = 1;

    if (argc > 1) fontPath  = argv[1];
    if (argc > 2) pointSize = atoi(argv[2]);
    if (argc > 3) width     = atoi(argv[3]);
    if (argc > 4) height    = atoi(argv[4]);
    if (argc > 5) depth     = atoi(argv[5]);
    if (pointSize < 4)  pointSize = 4;
    if (pointSize > 72) pointSize = 72;
    if (width  < 1) width  = 1;
    if (height < 1) height = 1;
    if (depth  < 1) depth  = 1;

    /* --- open libraries --- */
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39L);
    if (!GfxBase) {
        puts("ERROR: cannot open graphics.library v39+");
        ok = 0; goto cleanup;
    }

    LayersBase = OpenLibrary("layers.library", 39L);
    if (!LayersBase) {
        puts("ERROR: cannot open layers.library v39+");
        ok = 0; goto cleanup;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) {
        puts("ERROR: cannot open intuition.library v39+");
        ok = 0; goto cleanup;
    }

    URPBase = OpenLibrary("utf8rastport.library", 1L);
    if (!URPBase) {
        puts("ERROR: cannot open utf8rastport.library v1+");
        ok = 0; goto cleanup;
    }
    printf("utf8rastport.library opened: %p\n", (void *)URPBase);

    /* Phase 1 */
    if (!test_create_close_only(width, height, depth))
        ok = 0;

    /* Phase 2 */
    if (!test_draw_text(fontPath, pointSize, width, height, depth))
        ok = 0;

    /* --- sanity-check Intuition by opening a small window --- */
    printf("\n--- Phase 3: open Intuition window to verify system health ---\n");
    win = OpenWindowTags(NULL,
        WA_Left,    60L,
        WA_Top,     60L,
        WA_Width,   380L,
        WA_Height,  80L,
        WA_Title,   (ULONG)"testoffscreenbm: click close to quit",
        WA_Flags,   (ULONG)(WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_ACTIVATE),
        WA_IDCMP,   (ULONG)IDCMP_CLOSEWINDOW,
        TAG_DONE);

    if (!win) {
        puts("ERROR: OpenWindowTags() returned NULL – Intuition may be corrupted!");
        ok = 0; goto cleanup;
    }

    puts("  Window opened OK. Click the close gadget to finish.");
    WaitPort(win->UserPort);
    while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL)
        ReplyMsg((struct Message *)imsg);
    puts("  Window closed OK. System appears healthy.");

cleanup:
    if (win)          CloseWindow(win);
    if (URPBase)      CloseLibrary(URPBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (LayersBase)    CloseLibrary(LayersBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);

    return ok ? 0 : 1;
}
