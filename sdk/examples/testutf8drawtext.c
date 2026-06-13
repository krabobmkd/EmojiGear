/*
 * testutf8drawtext.c  –  validate the utf8rastport API + FreeType on AmigaOS 3.
 *
 * Opens an Intuition window, renders several UTF-8 strings (including
 * Latin, accented, and multi-byte Unicode characters) onto the window's
 * RastPort using the libutf8rastport static library.
 * This bypass the Amiga OS graphics SetFont()/Text() to offer
 * more modern font system, in an almost as simple way.
 *
 * Usage:
 *   testutf8drawtext [font.ttf] [pointsize]
 *
 * Defaults: first argument = "FONTS:helvetica.ttf", second = 18.
 *
 * Close the window's close gadget (or press any key) to quit.
 *
 * Written in C89 style for maximum Amiga compiler compatibility.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <intuition/intuition.h>
#include <graphics/rastport.h>
#include <graphics/gfx.h>

#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/utf8rastport.h>>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* You may need some kind of -I=EmojiGear:sdk/include compiler option to get those,
 or copy sdk/include in your favorite compiler include dir. */
#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

/* -------------------------------------------------------------------------
 * Amiga library bases (must be global for proto/ inline calls)
 * ------------------------------------------------------------------------- */
struct GfxBase       *GfxBase       = NULL;
struct IntuitionBase *IntuitionBase = NULL;
struct Library * URPBase  =NULL; 

/* -------------------------------------------------------------------------
 * Test strings
 * UTF-8 encoded inline – your editor must save this file as UTF-8.
 * The bytes shown in comments are the actual UTF-8 sequences used
 * as escape fallbacks if the source encoding is not UTF-8.
 * ------------------------------------------------------------------------- */

/* "Hello, Amiga!" in plain ASCII */
static const char *TEXT_BASIC = "\xF0\x9F\x98\x8A Hello, Amiga! That's it. We can go "
    "\xF0\x9F\x90\xB8 \xF0\x9F\x8C\xBA "
    ;

/* A few Latin extended characters: é à ü ñ (U+00E9 U+00E0 U+00FC U+00F1) */
static const char *TEXT_LATIN_EXT =
    "Latin ext: \xc3\xa9 \xc3\xa0 \xc3\xbc \xc3\xb1";

/* Some common Unicode symbols: ★ ✓ ♠ ♥ */
static const char *TEXT_SYMBOLS =
    "Symbols: \xe2\x98\x85 \xe2\x9c\x93 \xe2\x99\xa0 \xe2\x99\xa5 "
    "\xf0\x9f\x8c\x88 "
    ;

/* Arrow characters: ← → ↑ ↓ */
static const char *TEXT_ARROWS =
    "Arrows: \xe2\x86\x90 \xe2\x86\x92 \xe2\x86\x91 \xe2\x86\x93";
// 0xF0 0x9F 0x8C 0x88

/* -------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */
int main(int argc, char *argv[])
{
    const char *fontPath  = "LiberationSans-Regular.ttf";
    int         pointSize = 16;
    struct Window          *win = NULL;
    struct URPDrawContext   *dc  = NULL;
    struct URPTextMetric    metric;
    struct IntuiMessage    *imsg;
    int    running = 1;
    struct URPTextPos pos;

    /* --- parse arguments --- */
    if (argc > 1) fontPath  = argv[1];
    if (argc > 2) pointSize = atoi(argv[2]);
    if (pointSize < 4)  pointSize = 4;
    if (pointSize > 72) pointSize = 72;

    /* --- open libraries --- */
    GfxBase = (struct GfxBase *)OpenLibrary("graphics.library", 39L);
    if (!GfxBase) {
        puts("ERROR: cannot open graphics.library v39+");
        goto cleanup;
    }

    IntuitionBase = (struct IntuitionBase *)OpenLibrary("intuition.library", 39L);
    if (!IntuitionBase) {
        puts("ERROR: cannot open intuition.library v39+");
        goto cleanup;
    }

    /* this is the actual library to render utf8 text */
    URPBase  = OpenLibrary("utf8rastport.library", 4);
    if (!URPBase) {
        puts("ERROR: cannot open utf8rastport.library");
        goto cleanup;
    }
    /* --- open window --- */
    win = OpenWindowTags(NULL,
        WA_Left,         50L,
        WA_Top,          50L,
        WA_Width,        480L,
        WA_Height,       240L,
        WA_Title,        (ULONG)"test_urp - UTF-8 RastPort",
        WA_Flags,        (ULONG)(WFLG_CLOSEGADGET | WFLG_DRAGBAR |
                                 WFLG_DEPTHGADGET | WFLG_ACTIVATE),
        WA_IDCMP,        (ULONG)(IDCMP_CLOSEWINDOW | IDCMP_RAWKEY),
        TAG_DONE);

    if (!win) {
        puts("ERROR: cannot open window");
        goto cleanup;
    }

    /* --- create draw context --- */
    /* Pass NULL as friendBitmap: uses 1-bit / BltTemplate path (works everywhere) */
    dc = URPDC_Create(win->WScreen->RastPort.BitMap);
    if (!dc) {
        puts("ERROR: URPDC_Create() failed");
        goto cleanup;
    }

    URPDC_SetPreferenceFlags(dc,URP_PREF_ANTIALIAS | URP_PREF_CLUTMODE_NOMASK);
    /* if ever color indexed 8bit screen, need to remap glyphs to palette in a second glyph cache */
    URPDC_UpdateColorMap(dc,win->WScreen);

    /* --- load font --- 
     If you want to have texts with Emojis, this is important to understand
     you actually set up a list of multiple fonts (up to 8), the first  having the 
     highest priority, the others being the "fallback fonts". When an unicode point,
     that is to say a character index, is not find in the first font, wthe engine 
     try the second, etc... So you need a font for alphabet, maybe another for
     japanese characters, and finaly the Emoji font. 
     You can restart a font list on a DrawContext after a URPDC_FlushFonts() call.
    */
    printf("Loading font: %s  %d pt\n", fontPath, pointSize);
    if (!URPDC_AddFont(dc, fontPath, pointSize, 0)) {
        printf("ERROR: cannot load font '%s'\n", fontPath);
        goto cleanup;
    }
    if (!URPDC_AddFont(dc, "NotoColorEmoji32.ttf", pointSize, 0)) {
        printf("no Emoji font\n");
    } else
    {
        printf("Emoji font OK\n");
    }
    /* --- draw text samples --- */
    SetAPen(win->RPort, 1);   /* pen 1 = foreground colour */
    SetBPen(win->RPort, 0);   /* pen 0 = background (not used by BltTemplate) */
    SetDrMd(win->RPort, JAM1);

    {
     ULONG backGroundPen = 0;
     ULONG txtPen = 1;
     URPDC_SetDrawColorFromPen(dc,win->WScreen,txtPen,backGroundPen);
    }
    /* measure and draw each line */
    pos.y = (WORD)(win->BorderTop + 8 + pointSize);
    pos.x = (WORD)(win->BorderLeft + 8);
    URPDC_TextSizeUTF8(dc, TEXT_BASIC, -1, &metric);
    printf("Basic:    w=%d h=%d\n", (int)metric.width, (int)metric.height);
    URPDrawTextUTF8(win->RPort, dc, &pos, TEXT_BASIC, -1);

    pos.y = (WORD)( pos.y + metric.height + 4);
    pos.x = (WORD)(win->BorderLeft + 8);
    URPDC_TextSizeUTF8(dc, TEXT_LATIN_EXT, -1, &metric);
    printf("LatinExt: w=%d h=%d\n", (int)metric.width, (int)metric.height);
    URPDrawTextUTF8(win->RPort, dc, &pos, TEXT_LATIN_EXT, -1);

printf("\n\n   ***** TEXT_SYMBOLS START\n");
    pos.y = (WORD)( pos.y + metric.height + 4);
    pos.x = (WORD)(win->BorderLeft + 8);
    URPDC_TextSizeUTF8(dc, TEXT_SYMBOLS, -1, &metric);

 printf("\n\n");
    printf("Symbols:  w=%d h=%d\n", (int)metric.width, (int)metric.height);
    URPDrawTextUTF8(win->RPort, dc, &pos, TEXT_SYMBOLS, 1024);
printf("   ***** TEXT_SYMBOLS END\n\n\n");

    pos.y = (WORD)(pos.y + metric.height + 4);
    pos.x = (WORD)(win->BorderLeft + 8);
    URPDC_TextSizeUTF8(dc, TEXT_ARROWS, -1, &metric);
    printf("Arrows:   w=%d h=%d\n", (int)metric.width, (int)metric.height);
    URPDrawTextUTF8(win->RPort, dc, &pos, TEXT_ARROWS, -1);

    /* --- event loop: wait for close gadget or any key --- */
    while (running) {
        WaitPort(win->UserPort);
        while ((imsg = (struct IntuiMessage *)GetMsg(win->UserPort)) != NULL) {
            switch (imsg->Class) {
            case IDCMP_CLOSEWINDOW:
                running = 0;
                break;
            case IDCMP_RAWKEY:
                /* any key also closes */
                //running = 0;
                break;
            default:
                break;
            }
            ReplyMsg((struct Message *)imsg);
        }
    }

cleanup:
    if (dc)            URPDC_Release(dc);
    if (win)           CloseWindow(win);
    
    if (URPBase) CloseLibrary(URPBase);
    if (IntuitionBase) CloseLibrary((struct Library *)IntuitionBase);
    if (GfxBase)       CloseLibrary((struct Library *)GfxBase);

    return 0;
}
