/*
 * mmgemojibox.c – MUImojiGear emoji box window.
 *
 * Ported from EmojiGear/emojibox.c; key differences:
 *   - Uses MUI (MUIC_Window/Cycle/Boopsi/Group/Text) instead of BOOPSI layout.
 *   - Alt qualifier replaces Ctrl (Ctrl is reserved for MUI menu shortcuts).
 *   - Grid clicks flow through the MmgBoopsi queue (ICA_TARGET=MmgTargetInstance)
 *     exactly like UTEDN_* editor notifications, waking the main task via
 *     SIGBREAKF_CTRL_F.  The main loop reads EGRID_ClickedIdx from the message.
 *   - ANSI buttons are MUIC_Text objects; their pointers are kept in s_ansiBtn[]
 *     so Init() can wire MUIA_Pressed=FALSE → ReturnID(RID_EMOJI_ANSI_BASE+i).
 */

#include <string.h>
#include <stdio.h>

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <proto/utility.h>

#include <devices/inputevent.h>
#include <intuition/gadgetclass.h>
#include <intuition/cghooks.h>
#include <intuition/icclass.h>
#include <intuition/screens.h>

#include <libraries/mui.h>
/* MUI_NewObjectB is shared from muimojiGear.c – do NOT include proto/muimaster.h here
 * as it defines MUI_NewObject as a non-static inline, causing a duplicate symbol. */

#include <gadgets/unitexteditor.h>
#include <proto/unitexteditor.h>
#include <gadgets/unibutton.h>
#include <proto/unibutton.h>

/* Define URPBase locally so proto/utf8rastport.h pragmas use our own library base.
 * In UNITEXTEDITOR_SHARED=ON mode, the gadget owns its private URPBase; we open
 * a second reference ourselves so the grid's GM_RENDER can call URPDC_* safely. */
static struct Library *URPBase = NULL;
#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include "mmgemojibox.h"
#include "mmgboopsimessage.h"   /* MmgTargetInstance */
#include "muimojiGear.h"        /* struct App *app, App_GetRawEditorWin */

extern struct Task *mmgMainTask;  /* defined in muimojiGear.c */

/* Set by EmojiGrid_OnRender when called from a wrong process context;
 * cleared by MmgEmojiBox_FlushPendingRender() from the main task. */
static volatile BOOL s_pendingRender = FALSE;

/* =========================================================================
 * Register calling convention (same as mmgboopsimessage.c)
 * =========================================================================
 */
#ifdef __GNUC__
#  define REGARG(reg, decl) decl __asm(#reg)
#else
#  define REGARG(reg, decl) register __##reg decl
#endif

/* =========================================================================
 * Emoji set tables (40 entries each, row-major: idx = row*10 + col)
 * row 0 = no modifier, row 1 = Shift, row 2 = Alt, row 3 = Shift+Alt
 * =========================================================================
 */

static const char *popularEmojiTable[40] = {
    /* row 0: F1-F10, no modifier */
    "\xF0\x9F\x98\x80", /* 😀 U+1F600 */
    "\xF0\x9F\x98\x8A", /* 😊 U+1F60A */
    "\xF0\x9F\x98\x8D", /* 😍 U+1F60D */
    "\xF0\x9F\xA4\x94", /* 🤔 U+1F914 */
    "\xF0\x9F\x98\x8E", /* 😎 U+1F60E */
    "\xF0\x9F\xA5\xB3", /* 🥳 U+1F973 */
    "\xF0\x9F\x98\x8B", /* 😋 U+1F60B */
    "\xF0\x9F\x98\x8F", /* 😏 U+1F60F */
    "\xF0\x9F\x98\x82", /* 😂 U+1F602 */
    "\xF0\x9F\x98\xAA", /* 😪 U+1F62A */
    /* row 1: Shift */
    "\xF0\x9F\xA4\xA9", /* 🤩 U+1F929 */
    "\xF0\x9F\x98\x85", /* 😅 U+1F605 */
    "\xF0\x9F\x98\xB1", /* 😱  1F631 */
    "\xE2\x9D\xA4",     /* ❤  U+2764  */
    "\xF0\x9F\x91\x8D", /* 👍 U+1F44D */
    "\xF0\x9F\x99\x8F", /* 🙏 U+1F64F */
    "\xF0\x9F\x91\x80", /* 👀 U+1F440 */
    "\xF0\x9F\x9A\x80", /* 🚀 U+1F680 */
    "\xF0\x9F\x8E\x89", /* 🎉 U+1F389 */
    "\xF0\x9F\x92\x83", /* 💃 U+1F483 */
    "\xF0\x9F\x8D\x95", /* 🍕 U+1F355 */
    /* row 2: Ctrl */
    "\xF0\x9F\xA5\x82", /* 🥂 U+1F942 */

    /*"\xF0\x9F\x8D\xBE",  🍾 U+1F37E */
    "\xF0\x9F\x92\xAA", /* 💪 U+1F4AA */
    "\xE2\x98\x95",     /* ☕ U+2615  */
    "\xF0\x9F\x93\xB1", /* 📱 U+1F4F1 */
    "\xF0\x9F\x92\xBB", /* 💻 U+1F4BB */
    "\xF0\x9F\x92\xBE", /* 💾  */
    "\xF0\x9F\xA4\x96", /* robot */
    "\xF0\x9F\xA6\x84", /* 🦄 U+1F984 */
    "\xF0\x9F\x8C\x88", /* 🌈 U+1F308 */
    "\xF0\x9F\x8C\xB8", /* 🌸 U+1F338 */
    /* row 3: Ctrl+Shift */
    "\xF0\x9F\x92\xA9", /* 💩 U+1F4A9 */
    "\xF0\x9F\x8E\xB8", /* 🎸 U+1F3B8 */
    "\xF0\x9F\x92\x8E", /* 💎 U+1F48E */
    "\xF0\x9F\x8C\x99", /* 🌙 U+1F319 */
    "\xE2\xAD\x90",     /* ⭐ U+2B50  */
    "\xF0\x9F\x8E\xB5", /* 🎵 U+1F3B5 */
    "\xF0\x9F\x8C\x8D", /* 🌍 U+1F30D */
    "\xF0\x9F\x95\x99", /* 🕙 U+1F559 */
    "\xF0\x9F\x92\xA1", /* 💡 U+1F4A1 */
};

static const char *urbanEmojiTable[40] = {
    "\xF0\x9F\x9A\x97","\xF0\x9F\x9A\x95","\xF0\x9F\x9A\x8C","\xF0\x9F\x9A\x8E",
    "\xF0\x9F\x8F\x8E","\xF0\x9F\x9A\x93","\xF0\x9F\x9A\x91","\xF0\x9F\x9A\x92",
    "\xF0\x9F\x9A\x90","\xF0\x9F\x9A\x9A",
    "\xF0\x9F\x9A\x82","\xE2\x9C\x88",    "\xF0\x9F\x9A\x81","\xF0\x9F\x9B\xB8",
    "\xF0\x9F\x9A\xB2","\xF0\x9F\x9B\xB4","\xF0\x9F\x9B\xB5","\xF0\x9F\x8F\x8D",
    "\xF0\x9F\x9A\x80","\xF0\x9F\x9B\xBB",
    "\xF0\x9F\x8F\x99","\xF0\x9F\x8F\xA2","\xF0\x9F\x8F\xAC","\xF0\x9F\x8F\xA6",
    "\xF0\x9F\x8F\xA8","\xF0\x9F\x8F\xA9","\xF0\x9F\x8F\xAA","\xF0\x9F\x8F\xAB",
    "\xF0\x9F\x8F\x9B","\xF0\x9F\x95\x8C",
    "\xF0\x9F\x91\xB7","\xF0\x9F\x91\xAE","\xF0\x9F\x92\xBC","\xF0\x9F\x94\xA7",
    "\xF0\x9F\x94\xA8","\xF0\x9F\x8F\x97","\xF0\x9F\x9A\xA7","\xF0\x9F\x97\xBA",
    "\xF0\x9F\x93\xAB","\xF0\x9F\x9A\x89",
};
static const char *natureEmojiTable[40] = {
    "\xF0\x9F\x8C\xB8","\xF0\x9F\x8C\xBA","\xF0\x9F\x8C\xBB","\xF0\x9F\x8C\xB9",
    "\xF0\x9F\x8C\xB7","\xF0\x9F\x8C\xBC","\xF0\x9F\x8C\xBF","\xF0\x9F\x8D\x80",
    "\xF0\x9F\x8C\xB1","\xF0\x9F\x8C\xBE",
    "\xF0\x9F\x8D\x84","\xF0\x9F\x8C\xB5","\xF0\x9F\x8C\xB4","\xF0\x9F\x8C\xB2",
    "\xF0\x9F\x8C\xB3","\xF0\x9F\x8E\x8B","\xF0\x9F\x8E\x8D","\xF0\x9F\x8D\x81",
    "\xF0\x9F\x8D\x82","\xF0\x9F\x8D\x83",
    "\xF0\x9F\xA5\x95","\xF0\x9F\xA5\xA6","\xF0\x9F\x8C\xBD","\xF0\x9F\x8D\x85",
    "\xF0\x9F\xA5\x91","\xF0\x9F\x8D\x86","\xF0\x9F\xA5\x94","\xF0\x9F\xA7\x85",
    "\xF0\x9F\xA7\x84","\xF0\x9F\x8C\xB6",
    "\xE2\x9B\xB0",    "\xF0\x9F\x8F\x94","\xF0\x9F\x97\xBB","\xF0\x9F\x8C\x8B",
    "\xF0\x9F\x8F\x9D","\xF0\x9F\x8F\x9C","\xF0\x9F\x8C\x8A","\xF0\x9F\x8C\x85",
    "\xF0\x9F\x8C\x84","\xF0\x9F\x8C\x8C",
};
static const char *symbolsEmojiTable[40] = {
    "$","\xE2\x82\xAC","\xC2\xA3","\xC2\xA5","\xE2\x82\xA3",
    "\xE2\x82\xA4","\xE2\x82\xA6","\xE2\x82\xA9","\xE2\x82\xAA","\xE2\x82\xAB",
    "\xE2\x86\x92","\xE2\x86\x90","\xE2\x86\x91","\xE2\x86\x93","\xE2\x86\x94",
    "\xE2\x86\x95","\xE2\x87\x92","\xE2\x87\x90","\xE2\x87\x91","\xE2\x87\x93",
    "\xE2\x9C\x93","\xE2\x9C\x97","\xE2\x98\x85","\xE2\x98\x86","\xE2\x99\xA5",
    "\xE2\x99\xA6","\xE2\x99\xA0","\xE2\x99\xA3","\xC2\xA9",    "\xC2\xAE",
    "\xE2\x84\xA2","\xC2\xB0",    "\xC2\xA7",    "\xC2\xB6",    "\xE2\x80\xA0",
    "\xE2\x80\xA1","\xE2\x80\xA2","\xE2\x80\xA6","\xE2\x88\x9E","\xE2\x89\xA0",
};
static const char *sportsEmojiTable[40] = {
    "\xE2\x9A\xBD","\xF0\x9F\x8F\x80","\xF0\x9F\x8F\x88","\xE2\x9A\xBE",
    "\xF0\x9F\x8E\xBE","\xF0\x9F\x8F\x90","\xF0\x9F\x8F\x89","\xF0\x9F\x8E\xB1",
    "\xF0\x9F\x8F\x93","\xF0\x9F\x8F\xB8",
    "\xF0\x9F\xA5\x8A","\xF0\x9F\xA5\x8B","\xF0\x9F\xA4\xBC","\xF0\x9F\xA4\xB8",
    "\xF0\x9F\x8F\x8A","\xF0\x9F\x9A\xB4","\xF0\x9F\x8F\x83","\xF0\x9F\xA7\x97",
    "\xF0\x9F\xA4\xBA","\xE2\x9B\xB7",
    "\xF0\x9F\x8F\x86","\xF0\x9F\xA5\x87","\xF0\x9F\xA5\x88","\xF0\x9F\xA5\x89",
    "\xF0\x9F\x8F\x85","\xF0\x9F\x8E\xAF","\xF0\x9F\x8E\xB3","\xF0\x9F\x8F\x8B",
    "\xF0\x9F\xA4\xBE","\xF0\x9F\x8F\x84",
    "\xF0\x9F\xA7\x98","\xF0\x9F\x8F\x82","\xF0\x9F\xA4\xBF","\xF0\x9F\x8E\xBF",
    "\xE2\x9B\xB8",    "\xF0\x9F\x8F\x8C","\xF0\x9F\x8F\xB9","\xF0\x9F\xA5\x85",
    "\xF0\x9F\x8F\x91","\xF0\x9F\x8F\x92",
};
static const char *katakanaTable[40] = {
    "\xE3\x82\xA2","\xE3\x82\xA4","\xE3\x82\xA6","\xE3\x82\xA8","\xE3\x82\xAA",
    "\xE3\x82\xAB","\xE3\x82\xAD","\xE3\x82\xAF","\xE3\x82\xB1","\xE3\x82\xB3",
    "\xE3\x82\xB5","\xE3\x82\xB7","\xE3\x82\xB9","\xE3\x82\xBB","\xE3\x82\xBD",
    "\xE3\x82\xBF","\xE3\x83\x81","\xE3\x83\x84","\xE3\x83\x86","\xE3\x83\x88",
    "\xE3\x83\x8A","\xE3\x83\x8B","\xE3\x83\x8C","\xE3\x83\x8D","\xE3\x83\x8E",
    "\xE3\x83\x8F","\xE3\x83\x92","\xE3\x83\x95","\xE3\x83\x98","\xE3\x83\x9B",
    "\xE3\x83\x9E","\xE3\x83\x9F","\xE3\x83\xA0","\xE3\x83\xA1","\xE3\x83\xA2",
    "\xE3\x83\xA4","\xE3\x83\xA6","\xE3\x83\xA8","\xE3\x83\xA9","\xE3\x83\xAA",
};
static const char *cookingEmojiTable[40] = {
    "\xF0\x9F\x8D\xB3","\xF0\x9F\xA5\x98","\xF0\x9F\xAB\x95","\xF0\x9F\x8D\xB2",
    "\xF0\x9F\xA5\x97","\xF0\x9F\xAB\x99","\xF0\x9F\xA7\x82","\xF0\x9F\xA7\x88",
    "\xF0\x9F\xAB\x92","\xF0\x9F\x8D\xB1",
    "\xF0\x9F\xA5\xA9","\xF0\x9F\x8D\x97","\xF0\x9F\x8D\x96","\xF0\x9F\xA5\x9A",
    "\xF0\x9F\xA7\x80","\xF0\x9F\x8D\xA3","\xF0\x9F\x8D\xA4","\xF0\x9F\xA6\x90",
    "\xF0\x9F\xA6\x91","\xF0\x9F\xA5\x93",
    "\xF0\x9F\x8D\x9E","\xF0\x9F\xA5\x90","\xF0\x9F\xA5\x96","\xF0\x9F\xA7\x81",
    "\xF0\x9F\x8E\x82","\xF0\x9F\x8D\xB0","\xF0\x9F\x8D\xA9","\xF0\x9F\x8D\xAA",
    "\xF0\x9F\x8D\xAB","\xF0\x9F\x8D\xAD",
    "\xF0\x9F\x8D\x87","\xF0\x9F\x8D\x93","\xF0\x9F\x8D\x8E","\xF0\x9F\x8D\x8A",
    "\xF0\x9F\x8D\x8B","\xE2\x98\x95",    "\xF0\x9F\x8D\xB5","\xF0\x9F\xA7\x83",
    "\xF0\x9F\x8D\xBA","\xF0\x9F\x8D\xB7",
};
static const char *cultureEmojiTable[40] = {
    "\xF0\x9F\x8E\xAD","\xF0\x9F\x8E\xAC","\xF0\x9F\x8E\xA4","\xF0\x9F\x8E\xA7",
    "\xF0\x9F\x8E\xB9","\xF0\x9F\x8E\xB8","\xF0\x9F\x8E\xBA","\xF0\x9F\xA5\x81",
    "\xF0\x9F\x8E\xBB","\xF0\x9F\xAA\x95",
    "\xF0\x9F\x8E\xA8","\xF0\x9F\x96\x8C","\xF0\x9F\x93\x9A","\xF0\x9F\x93\x96",
    "\xF0\x9F\x97\xBF","\xF0\x9F\x8F\xBA","\xF0\x9F\x96\xBC","\xF0\x9F\x8E\xAA",
    "\xF0\x9F\x8E\xA1","\xF0\x9F\x8E\xA2",
    "\xF0\x9F\x8F\xB0","\xF0\x9F\x97\xBC","\xF0\x9F\x97\xBD","\xE2\x9B\xA9",
    "\xF0\x9F\x95\x8D","\xF0\x9F\x8F\xAF","\xF0\x9F\x8C\x89","\xF0\x9F\x8E\x87",
    "\xF0\x9F\x8E\x86","\xF0\x9F\x8E\x91",
    "\xE2\x99\x9F",    "\xF0\x9F\x8E\xB2","\xF0\x9F\x83\x8F","\xF0\x9F\xA7\xA9",
    "\xF0\x9F\x8E\xB0","\xF0\x9F\x8E\x8A","\xF0\x9F\x8E\x89","\xF0\x9F\x8E\x83",
    "\xF0\x9F\x8E\x84","\xF0\x9F\xA7\xA7",
};
static const char *yearEventsTable[40] = {
    "\xF0\x9F\x8E\x84","\xF0\x9F\x8E\x85","\xF0\x9F\xA4\xB6","\xF0\x9F\x8E\x81",
    "\xE2\x9B\x84",    "\xE2\x9D\x84",    "\xF0\x9F\x8C\x9F","\xF0\x9F\x95\xAF",
    "\xF0\x9F\xA7\xA6","\xF0\x9F\x94\x94",
    "\xF0\x9F\x8E\x86","\xF0\x9F\x8E\x8A","\xF0\x9F\x8E\x89","\xF0\x9F\xA5\x82",
    "\xF0\x9F\x8D\xBE","\xE2\x9D\xA4",    "\xF0\x9F\x92\x9D","\xF0\x9F\x92\x8C",
    "\xF0\x9F\x8C\xB9","\xF0\x9F\x92\x98",
    "\xF0\x9F\x90\xA3","\xF0\x9F\x90\xB0","\xF0\x9F\xA5\x9A","\xF0\x9F\x8C\xB7",
    "\xF0\x9F\x8C\xB8","\xE2\x98\x98",    "\xF0\x9F\x94\xA5","\xF0\x9F\xA6\x8B",
    "\xF0\x9F\x90\x9D","\xF0\x9F\x8C\xBA",
    "\xF0\x9F\x8E\x83","\xF0\x9F\x91\xBB","\xF0\x9F\xA6\x87","\xF0\x9F\x95\xB7",
    "\xF0\x9F\x8D\x82","\xF0\x9F\x8D\x81","\xF0\x9F\xA6\x83","\xF0\x9F\xAA\x94",
    "\xF0\x9F\x95\x8E","\xF0\x9F\x8E\x91",
};
static const char *businessEmojiTable[40] = {
    "\xF0\x9F\x92\xBB","\xF0\x9F\x96\xA5","\xF0\x9F\x96\xA8","\xE2\x8C\xA8",
    "\xF0\x9F\x96\xB1","\xF0\x9F\x92\xBE","\xF0\x9F\x92\xBF","\xF0\x9F\x93\xB1",
    "\xF0\x9F\x96\xB2","\xF0\x9F\x93\xA1",
    "\xF0\x9F\x8C\x90","\xF0\x9F\x93\xB6","\xF0\x9F\x94\x92","\xF0\x9F\x94\x93",
    "\xF0\x9F\x94\x91","\xE2\x9A\x99",    "\xF0\x9F\x9B\xA0","\xF0\x9F\xA4\x96",
    "\xF0\x9F\xA7\xA0","\xF0\x9F\x92\xA1",
    "\xF0\x9F\x93\x8A","\xF0\x9F\x93\x88","\xF0\x9F\x93\x89","\xF0\x9F\x92\xB0",
    "\xF0\x9F\x92\xB3","\xF0\x9F\x92\xB5","\xF0\x9F\x93\x8B","\xF0\x9F\x93\x81",
    "\xF0\x9F\x97\x82","\xF0\x9F\x8F\xA6",
    "\xF0\x9F\x91\x94","\xF0\x9F\x92\xBC","\xF0\x9F\x93\xA7","\xF0\x9F\x93\xA8",
    "\xF0\x9F\x93\x85","\xE2\x9C\x8F",    "\xF0\x9F\x93\x9D","\xF0\x9F\x96\x8A",
    "\xF0\x9F\x97\x92","\xF0\x9F\x93\x8C",
};
static const char *animalsEmojiTable[40] = {
    "\xF0\x9F\x90\xB6","\xF0\x9F\x90\xB1","\xF0\x9F\x90\xAD","\xF0\x9F\x90\xB0",
    "\xF0\x9F\xA6\x8A","\xF0\x9F\x90\xBB","\xF0\x9F\x90\xBC","\xF0\x9F\x90\xA8",
    "\xF0\x9F\x90\xAF","\xF0\x9F\xA6\x81",
    "\xF0\x9F\x90\xAE","\xF0\x9F\x90\xB7","\xF0\x9F\x90\xB8","\xF0\x9F\x90\xB5",
    "\xF0\x9F\x99\x88","\xF0\x9F\x90\x94","\xF0\x9F\x90\xA7","\xF0\x9F\x90\xA6",
    "\xF0\x9F\xA6\x86","\xF0\x9F\xA6\x85",
    "\xF0\x9F\x90\xAC","\xF0\x9F\x90\xB3","\xF0\x9F\xA6\x88","\xF0\x9F\x90\xA0",
    "\xF0\x9F\x90\x99","\xF0\x9F\xA6\x80","\xF0\x9F\xA6\x8B","\xF0\x9F\x90\x9D",
    "\xF0\x9F\x90\x9B","\xF0\x9F\xA6\x97",
    "\xF0\x9F\x90\x8A","\xF0\x9F\x90\xA2","\xF0\x9F\xA6\x8E","\xF0\x9F\x90\x8D",
    "\xF0\x9F\xA6\x93","\xF0\x9F\xA6\x92","\xF0\x9F\x90\x98","\xF0\x9F\xA6\x8F",
    "\xF0\x9F\xA6\x94","\xF0\x9F\xA6\x98",
};

typedef struct { const char *name; const char **emojis; } EmojiSetDesc;
static const EmojiSetDesc emojiSets[EMOJIBOX_NUM_SETS] = {
    {"Popular",    popularEmojiTable }, {"Urban",    urbanEmojiTable   },
    {"Nature",     natureEmojiTable  }, {"Symbols",  symbolsEmojiTable },
    {"Sports",     sportsEmojiTable  }, {"Katakana", katakanaTable     },
    {"Cooking",    cookingEmojiTable }, {"Culture",  cultureEmojiTable },
    {"Year Events",yearEventsTable   }, {"Business", businessEmojiTable},
    {"Animals",    animalsEmojiTable },
};

/* Stable NULL-terminated name list for MUIC_Cycle */
static const char *s_setNames[EMOJIBOX_NUM_SETS + 1];

/* =========================================================================
 * Private EmojiGrid BOOPSI gadget
 * =========================================================================
 */
#define EGRID_HDR_COL_MIN  72
#define EGRID_HDR_ROW_MIN  14
#define EGRID_CELL_MIN_W   22
#define EGRID_CELL_MIN_H   24

typedef struct {
    const char          **emojis;
    struct URPDrawContext *dc;
    int                   clickedIdx;
    ULONG                 clickSeq;
    WORD  headerColW, headerRowH, cellW, cellH;
    WORD  fontHeight, fontAscent;
    struct Screen *screen;
    UWORD pens[4]; /* 0=BG 1=SHINE 2=SHADOW 3=TEXT */
} EmojiGridInst;
#define EP_BG     0
#define EP_SHINE  1
#define EP_SHADOW 2
#define EP_TEXT   3

#define EGRID_INST(cl,o) ((EmojiGridInst *)INST_DATA((cl),(o)))
#define EG(o)            ((struct Gadget *)(o))

static const char *s_colLabels[10] = {"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10"};
/* Ctrl replaced by Alt – Ctrl is consumed by MUI for menu shortcuts */
static const char *s_rowLabels[4] = {"-","Shift","Alt","Sh+Alt"};

/* Safe to call from input.device context (only Signal + array write in target) */
static void egrid_notify(Class *cl, Object *o, struct GadgetInfo *gi,
                         ULONG attr, ULONG val)
{
    struct TagItem   tags[3];
    struct opUpdate  upd;
    tags[0].ti_Tag  = attr;       tags[0].ti_Data = val;
    tags[1].ti_Tag  = GA_ID;      tags[1].ti_Data = (ULONG)EG(o)->GadgetID;
    tags[2].ti_Tag  = TAG_END;    tags[2].ti_Data = 0;
    upd.MethodID     = OM_NOTIFY;
    upd.opu_AttrList = tags;
    upd.opu_GInfo    = gi;
    upd.opu_Flags    = 0;
    DoSuperMethodA(cl, o, (APTR)&upd);
}

static ULONG EmojiGrid_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    EmojiGridInst *inst = EGRID_INST(cl, o);
    struct RastPort *rp  = msg->gpr_RPort;
    struct Gadget   *g   = EG(o);
    struct Screen   *scr = msg->gpr_GInfo ? msg->gpr_GInfo->gi_Screen : NULL;
    struct DrawInfo *dri = msg->gpr_GInfo ? msg->gpr_GInfo->gi_DrInfo : NULL;
    WORD gx = g->LeftEdge, gy = g->TopEdge, gw = g->Width, gh = g->Height;
    WORD col, row, fontH, baseLine;
    (void)cl;

    /* utf8rastport requires the correct process context.
     * If called from any other task (e.g. Intuition refresh in input context),
     * defer by flagging a pending render and waking the main task. */
    if (FindTask(NULL) != mmgMainTask) {
        s_pendingRender = TRUE;
        if (mmgMainTask) Signal(mmgMainTask, SIGBREAKF_CTRL_F);
        return 0;
    }

    if (!rp || gw <= 0 || gh <= 0) return 0;
    if (dri) {
        inst->pens[EP_BG]     = dri->dri_Pens[BACKGROUNDPEN];
        inst->pens[EP_SHINE]  = dri->dri_Pens[SHINEPEN];
        inst->pens[EP_SHADOW] = dri->dri_Pens[SHADOWPEN];
        inst->pens[EP_TEXT]   = dri->dri_Pens[TEXTPEN];
    }
    if (scr && inst->dc && scr != inst->screen) {
        struct URPTextMetric m;
        inst->screen = scr;
        URPDC_SetDrawScreen(inst->dc, scr);
        URPDC_GetFontLineMetrics(inst->dc, &m);
        if (m.height <= 0) URPDC_TextSizeUTF8(inst->dc, "Agpqj", -1, &m);
        inst->fontHeight = m.height > 0 ? m.height : 14;
        inst->fontAscent = m.baseY  > 0 ? m.baseY  : inst->fontHeight;
    }
    fontH    = rp->Font ? (WORD)rp->Font->tf_YSize    : 8;
    baseLine = rp->Font ? (WORD)rp->Font->tf_Baseline : 6;

    /* Layout */
    inst->headerRowH = fontH + 6;
    { WORD mx=0, ri;
      for (ri=0; ri<4; ri++) {
        WORD tw=(WORD)TextLength(rp,s_rowLabels[ri],(ULONG)strlen(s_rowLabels[ri]));
        if(tw>mx)mx=tw; }
      inst->headerColW = mx+14;
      if(inst->headerColW < EGRID_HDR_COL_MIN) inst->headerColW=EGRID_HDR_COL_MIN; }
    { WORD cw=gw-inst->headerColW, ch=gh-inst->headerRowH;
      inst->cellW=(cw>0)?cw/10:0; inst->cellH=(ch>0)?ch/4:0; }

    SetAPen(rp,inst->pens[EP_BG]); SetDrMd(rp,JAM1);
    RectFill(rp,(LONG)gx,(LONG)gy,(LONG)(gx+gw-1),(LONG)(gy+gh-1));
    if(inst->cellW<=0||inst->cellH<=0) return 0;

    /* Header row */
    for(col=0;col<10;col++){
        WORD cx=gx+inst->headerColW+col*inst->cellW, cw=inst->cellW,ch=inst->headerRowH;
        const char *l=s_colLabels[col]; WORD tw,tx,ty;
        SetAPen(rp,inst->pens[EP_SHINE]); SetDrMd(rp,JAM1);
        RectFill(rp,(LONG)cx,(LONG)gy,(LONG)(cx+cw-1),(LONG)(gy+ch-1));
        tw=(WORD)TextLength(rp,l,(ULONG)strlen(l));
        tx=cx+(cw-tw)/2; ty=gy+(ch-fontH)/2+baseLine;
        SetAPen(rp,inst->pens[EP_SHADOW]); SetBPen(rp,inst->pens[EP_SHINE]); SetDrMd(rp,JAM2);
        Move(rp,(LONG)tx,(LONG)ty); Text(rp,l,(ULONG)strlen(l));
    }
    /* Header column */
    for(row=0;row<4;row++){
        WORD ry=gy+inst->headerRowH+row*inst->cellH,rw=inst->headerColW,rh=inst->cellH;
        const char *l=s_rowLabels[row]; WORD tw,tx,ty;
        SetAPen(rp,inst->pens[EP_SHINE]); SetDrMd(rp,JAM1);
        RectFill(rp,(LONG)gx,(LONG)ry,(LONG)(gx+rw-1),(LONG)(ry+rh-1));
        tw=(WORD)TextLength(rp,l,(ULONG)strlen(l));
        tx=gx+(rw-tw)/2; ty=ry+(rh-fontH)/2+baseLine;
        SetAPen(rp,inst->pens[EP_SHADOW]); SetBPen(rp,inst->pens[EP_SHINE]); SetDrMd(rp,JAM2);
        Move(rp,(LONG)tx,(LONG)ty); Text(rp,l,(ULONG)strlen(l));
    }
    /* Corner */
    SetAPen(rp,inst->pens[EP_SHINE]); SetDrMd(rp,JAM1);
    RectFill(rp,(LONG)gx,(LONG)gy,(LONG)(gx+inst->headerColW-1),(LONG)(gy+inst->headerRowH-1));

    /* Emoji cells */
    if(inst->emojis && inst->dc && inst->screen){
        URPDC_UpdateColorMap(inst->dc,inst->screen);
        URPDC_SetDrawColorFromPen(inst->dc,inst->screen,(LONG)inst->pens[EP_TEXT],(LONG)inst->pens[EP_BG]);
        SetAPen(rp,inst->pens[EP_TEXT]); SetBPen(rp,inst->pens[EP_BG]); SetDrMd(rp,JAM2);
        for(row=0;row<4;row++) for(col=0;col<10;col++){
            const char *e=inst->emojis[row*10+col];
            struct URPTextMetric m; struct URPTextPos pos; WORD cx,cy;
            if(!e||!e[0]) continue;
            cx=gx+inst->headerColW+col*inst->cellW;
            cy=gy+inst->headerRowH+row*inst->cellH;
            URPDC_TextSizeUTF8(inst->dc,e,-1,&m);
            pos.x=(WORD)(cx+(inst->cellW-m.width)/2);
            pos.y=(WORD)(cy+(inst->cellH-inst->fontHeight)/2+inst->fontAscent);
            if(pos.x<cx) pos.x=cx;
            if(pos.y<cy+inst->fontAscent) pos.y=cy+inst->fontAscent;
            URPDC_SetDrawColorFromPen(inst->dc,inst->screen,(LONG)inst->pens[EP_TEXT],(LONG)inst->pens[EP_BG]);
            URPDrawTextUTF8(rp,inst->dc,&pos,e,(ULONG)(-1));
        }
    }
    /* Grid lines */
    { WORD gr=gx+inst->headerColW+10*inst->cellW, gb=gy+inst->headerRowH+4*inst->cellH;
      SetAPen(rp,inst->pens[EP_SHADOW]); SetDrMd(rp,JAM1);
      Move(rp,(LONG)gx,(LONG)gy);  Draw(rp,(LONG)gr,(LONG)gy);
      Draw(rp,(LONG)gr,(LONG)gb);  Draw(rp,(LONG)gx,(LONG)gb);
      Draw(rp,(LONG)gx,(LONG)gy);
      Move(rp,(LONG)(gx+inst->headerColW),(LONG)gy);
      Draw(rp,(LONG)(gx+inst->headerColW),(LONG)gb);
      Move(rp,(LONG)gx,(LONG)(gy+inst->headerRowH));
      Draw(rp,(LONG)gr,(LONG)(gy+inst->headerRowH));
      for(col=1;col<10;col++){ WORD lx=gx+inst->headerColW+col*inst->cellW;
        Move(rp,(LONG)lx,(LONG)(gy+inst->headerRowH)); Draw(rp,(LONG)lx,(LONG)gb); }
      for(row=1;row<4;row++){ WORD ly=gy+inst->headerRowH+row*inst->cellH;
        Move(rp,(LONG)gx,(LONG)ly); Draw(rp,(LONG)gr,(LONG)ly); } }
    return 0;
}

static ULONG EmojiGrid_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    EmojiGridInst *inst = EGRID_INST(cl, o);
    WORD mx=msg->gpi_Mouse.X, my=msg->gpi_Mouse.Y, col, row;
    if(!msg->gpi_IEvent) return GMR_NOREUSE;
    if(inst->cellW<=0||inst->cellH<=0) return GMR_NOREUSE;
    if(mx<inst->headerColW||my<inst->headerRowH) return GMR_NOREUSE;
    col=(WORD)((mx-inst->headerColW)/inst->cellW); if(col>9)col=9;
    row=(WORD)((my-inst->headerRowH)/inst->cellH); if(row>3)row=3;
    inst->clickedIdx=(int)(row*10+col);
    inst->clickSeq++;
    /* Notify via MmgBoopsi queue – safe in input.device context */
    egrid_notify(cl,o,msg->gpi_GInfo,EGRID_ClickedIdx,(ULONG)inst->clickedIdx);
    egrid_notify(cl,o,msg->gpi_GInfo,EGRID_ClickSeq,  inst->clickSeq);
    *msg->gpi_Termination=0;
    return GMR_MEACTIVE;
}

static ULONG EmojiGrid_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    struct InputEvent *ie=msg->gpi_IEvent;
    (void)cl;(void)o;
    if(!ie) return GMR_MEACTIVE;
    if(ie->ie_Class==IECLASS_RAWMOUSE){
        if(ie->ie_Code==(IECODE_LBUTTON|IECODE_UP_PREFIX)){
            *msg->gpi_Termination=0; return GMR_NOREUSE|GMR_VERIFY; }
        if(ie->ie_Code==IECODE_RBUTTON) return GMR_REUSE;
    }
    return GMR_MEACTIVE;
}

static ULONG EmojiGrid_OnGoInactive(Class *cl,Object *o,struct gpGoInactive *msg)
{ (void)cl;(void)o;(void)msg; return 0; }

static ULONG EmojiGrid_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    struct IBox *d=&msg->gpd_Domain;
    (void)cl;(void)o;
    switch(msg->gpd_Which){
    case GDOMAIN_MINIMUM:
        d->Width =EGRID_HDR_COL_MIN+10*EGRID_CELL_MIN_W;
        d->Height=EGRID_HDR_ROW_MIN+4 *EGRID_CELL_MIN_H; break;
    case GDOMAIN_NOMINAL:
        d->Width =EGRID_HDR_COL_MIN+10*EGRID_CELL_MIN_W*2;
        d->Height=EGRID_HDR_ROW_MIN+4 *EGRID_CELL_MIN_H*2; break;
    default: d->Width=32767; d->Height=32767; break;
    }
    return 0;
}

static ULONG EmojiGrid_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    Object *self=(Object *)DoSuperMethodA(cl,o,(APTR)msg);
    if(self){ EmojiGridInst *inst=EGRID_INST(cl,self);
      struct TagItem *t;
      memset(inst,0,sizeof(EmojiGridInst)); inst->clickedIdx=-1;
      if((t=FindTagItem(EGRID_EmojiSet,msg->ops_AttrList))!=NULL)
          inst->emojis=(const char **)t->ti_Data; }
    return (ULONG)self;
}
static ULONG EmojiGrid_OnDispose(Class *cl,Object *o,Msg msg)
{
     EmojiGridInst *inst=EGRID_INST(cl,o);
    if(inst->dc) URPDC_Release(inst->dc);
    inst->dc = NULL;
    return DoSuperMethodA(cl,o,(APTR)msg);
}

static ULONG EmojiGrid_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    EmojiGridInst *inst=EGRID_INST(cl,o);
    struct TagItem *state=msg->ops_AttrList,*tag;
    BOOL redraw=FALSE;
    while((tag=NextTagItem(&state))!=NULL){
        switch(tag->ti_Tag){
        case EGRID_EmojiSet:
            inst->emojis=(const char **)tag->ti_Data; redraw=TRUE; break;
        case EGRID_URPDrawContext:
            if(inst->dc != (struct URPDrawContext *)tag->ti_Data)
            {
                if(inst->dc) URPDC_Release(inst->dc);
                inst->dc=(struct URPDrawContext *)tag->ti_Data;
                if(inst->dc) URPDC_Retain(inst->dc);
                inst->screen=NULL;
                redraw=TRUE;
            }
            break;
        default: break;
        }
    }
    if(redraw&&msg->ops_GInfo){
        // struct RastPort *rp=ObtainGIRPort(msg->ops_GInfo);
        // if(rp){ DoMethod(o,GM_RENDER,msg->ops_GInfo,rp,GREDRAW_REDRAW);
        //         ReleaseGIRPort(rp); }
    }
    return DoSuperMethodA(cl,o,(APTR)msg);
}
static ULONG EmojiGrid_OnGet(Class *cl, Object *o, struct opGet *msg)
{
    EmojiGridInst *inst=EGRID_INST(cl,o);
    switch(msg->opg_AttrID){
    case EGRID_EmojiSet:   *msg->opg_Storage=(ULONG)inst->emojis;     return TRUE;
    case EGRID_ClickedIdx: *msg->opg_Storage=(ULONG)inst->clickedIdx; return TRUE;
    case EGRID_ClickSeq:   *msg->opg_Storage=inst->clickSeq;          return TRUE;
    default: break;
    }
    return DoSuperMethodA(cl,o,(APTR)msg);
}

static ULONG EmojiGrid_Dispatch(
    REGARG(a0, Class *cl),
    REGARG(a2, Object *o),
    REGARG(a1, Msg msg))
{
    switch(msg->MethodID){
    case OM_NEW:         return EmojiGrid_OnNew        (cl,o,(struct opSet *)msg);
    case OM_DISPOSE:     return EmojiGrid_OnDispose     (cl,o,msg);
    case OM_SET:
    case OM_UPDATE:      return EmojiGrid_OnSet         (cl,o,(struct opSet *)msg);
    case OM_GET:         return EmojiGrid_OnGet         (cl,o,(struct opGet *)msg);
    case GM_RENDER:      return EmojiGrid_OnRender      (cl,o,(struct gpRender *)msg);
    case GM_GOACTIVE:    return EmojiGrid_OnGoActive    (cl,o,(struct gpInput *)msg);
    case GM_HANDLEINPUT: return EmojiGrid_OnHandleInput (cl,o,(struct gpInput *)msg);
    case GM_GOINACTIVE:  return EmojiGrid_OnGoInactive  (cl,o,(struct gpGoInactive *)msg);
    case GM_DOMAIN:      return EmojiGrid_OnDomain      (cl,o,(struct gpDomain *)msg);
    case GM_HITTEST:     return GMR_GADGETHIT;
    default:             return DoSuperMethodA(cl,o,(APTR)msg);
    }
}

/* =========================================================================
 * ANSI escape strings and button labels
 * =========================================================================
 */
static const char * const s_ansiStrings[MMG_NUM_ANSI_BTNS] = {
    "\x1B[1m",  "\x1B[3m",  "\x1B[4m",  "\x1B[7m",   /* Bold Italic Under. Inv. */
    "\x1B[34m", "\x1B[31m", "\x1B[32m", "\x1B[33m",  /* Blue Red Green Yellow   */
    "\x1B[37m", "\x1B[30m", "\x1B[0m",               /* White Black Normal      */
};
static const char * const s_ansiLabels[MMG_NUM_ANSI_BTNS] = {
    "Bold","Italic","Under.","Inv.",
    "Blue","Red","Green","Yellow","White","Black","Normal"
};

/* =========================================================================
 * Module state
 * =========================================================================
 */
static Class                *s_gridClass  = NULL;
static struct URPDrawContext *s_dc         = NULL;
static int                   s_currentSet = 0;
static Object               *s_ansiBtn[MMG_NUM_ANSI_BTNS];

static Object *make_ansi_btn(const char *label)
{
    return MUI_NewObjectB(MUIC_Text,
        MUIA_Text_Contents, (ULONG)label,
        MUIA_Text_PreParse, (ULONG)"\033c",
        MUIA_Frame,         MUIV_Frame_Button,
        MUIA_InputMode,     MUIV_InputMode_RelVerify,
        MUIA_Background,    MUII_ButtonBack,
        TAG_DONE);
}

/* =========================================================================
 * MmgEmojiBox_BuildWindow
 * =========================================================================
 */
Object *MmgEmojiBox_BuildWindow(void)
{
    Object *cycleObj=NULL, *gridObj=NULL, *win=NULL;
    Object *ansiRow1=NULL, *ansiRow2=NULL, *ansiGroup=NULL;
    int i;

    /* Open utf8rastport.library for our local URPBase (shared gadget mode) */
    if(!URPBase) URPBase = OpenLibrary("utf8rastport.library", 4);
    if(!URPBase) { /* emoji rendering won't work but we can still show the window */ }

    /* Grid BOOPSI class */
    if(s_gridClass == NULL)
        s_gridClass = MakeClass(NULL,"gadgetclass",NULL,sizeof(EmojiGridInst),0);
    if(!s_gridClass) return NULL;
    s_gridClass->cl_Dispatcher.h_Entry = (HOOKFUNC)EmojiGrid_Dispatch;

    /* Stable name list for cycle */
    for(i=0;i<EMOJIBOX_NUM_SETS;i++) s_setNames[i]=emojiSets[i].name;
    s_setNames[EMOJIBOX_NUM_SETS]=NULL;

    /* Set selector cycle */
    cycleObj = MUI_NewObjectB(MUIC_Cycle,
        MUIA_Cycle_Entries, (ULONG)s_setNames,
        MUIA_Cycle_Active,  0UL,
        TAG_DONE);

    /* Emoji grid */
    gridObj = MUI_NewObjectB(MUIC_Boopsi,
        MUIA_Boopsi_Class,     (ULONG)s_gridClass,
        MUIA_Boopsi_MinWidth,  (ULONG)(EGRID_HDR_COL_MIN+10*EGRID_CELL_MIN_W),
        MUIA_Boopsi_MinHeight, (ULONG)(EGRID_HDR_ROW_MIN+4 *EGRID_CELL_MIN_H),
        MUIA_Boopsi_Smart,     TRUE,
        MUIA_Boopsi_Remember,  EGRID_EmojiSet,     /* restore current set after gadget recreation */
        MUIA_Boopsi_Remember,  EGRID_ClickedIdx,
        MUIA_Boopsi_Remember,  EGRID_ClickSeq,
        GA_ID,                 3UL,                    /* GID_EMOJI_GRID */
        ICA_TARGET,            (ULONG)MmgTargetInstance,
        EGRID_EmojiSet,        (ULONG)emojiSets[0].emojis,
        TAG_DONE);

    /* ANSI buttons (stored so Init can wire notifications) */
    for(i=0;i<MMG_NUM_ANSI_BTNS;i++)
        s_ansiBtn[i] = make_ansi_btn(s_ansiLabels[i]);

    /* Row 1: Bold Italic Under. Inv. (indices 0-3) */
    ansiRow1 = MUI_NewObjectB(MUIC_Group,
        MUIA_Group_Horiz,    TRUE,
        MUIA_Group_SameSize, TRUE,
        MUIA_Group_Spacing,  1,
        MUIA_Group_Child, (ULONG)s_ansiBtn[0],
        MUIA_Group_Child, (ULONG)s_ansiBtn[1],
        MUIA_Group_Child, (ULONG)s_ansiBtn[2],
        MUIA_Group_Child, (ULONG)s_ansiBtn[3],
        TAG_DONE);

    /* Row 2: Blue Red Green Yellow White Black Normal (indices 4-10) */
    ansiRow2 = MUI_NewObjectB(MUIC_Group,
        MUIA_Group_Horiz,    TRUE,
        MUIA_Group_SameSize, TRUE,
        MUIA_Group_Spacing,  1,
        MUIA_Group_Child, (ULONG)s_ansiBtn[4],
        MUIA_Group_Child, (ULONG)s_ansiBtn[5],
        MUIA_Group_Child, (ULONG)s_ansiBtn[6],
        MUIA_Group_Child, (ULONG)s_ansiBtn[7],
        MUIA_Group_Child, (ULONG)s_ansiBtn[8],
        MUIA_Group_Child, (ULONG)s_ansiBtn[9],
        MUIA_Group_Child, (ULONG)s_ansiBtn[10],
        TAG_DONE);

    ansiGroup = MUI_NewObjectB(MUIC_Group,
        MUIA_Group_Spacing, 1,
        MUIA_Frame,         MUIV_Frame_Group,
        MUIA_FrameTitle,    (ULONG)"ANSI",
        MUIA_Group_Child,   (ULONG)ansiRow1,
        MUIA_Group_Child,   (ULONG)ansiRow2,
        TAG_DONE);

    win = MUI_NewObjectB(MUIC_Window,
        MUIA_Window_Title,  (ULONG)"Emoji Box",
        MUIA_Window_ID,     MAKE_ID('E','M','B','X'),
        MUIA_Window_RootObject, (ULONG)MUI_NewObjectB(MUIC_Group,
            MUIA_Group_Spacing, 2,
            MUIA_Group_Child, (ULONG)MUI_NewObjectB(MUIC_Group,
                MUIA_Group_Horiz,   TRUE,
                MUIA_Group_Spacing, 4,
                MUIA_Group_Child, (ULONG)MUI_NewObjectB(MUIC_Text,
                    MUIA_Text_Contents, (ULONG)"Set:",
                    MUIA_Weight, 0UL, TAG_DONE),
                MUIA_Group_Child, (ULONG)cycleObj,
                TAG_DONE),
            MUIA_Group_Child, (ULONG)gridObj,
            MUIA_Group_Child, (ULONG)ansiGroup,
            TAG_DONE),
        TAG_DONE);

    if(!win){
        FreeClass(s_gridClass); s_gridClass=NULL;
        return NULL;
    }

    if(app){
        app->emojiBoxWinObj   = win;
        app->emojiBoxCycleObj = cycleObj;
        app->emojiBoxGridObj  = gridObj;
    }
    return win;
}

/* =========================================================================
 * MmgEmojiBox_Init – wire MUI notifications; load URPDrawContext.
 * =========================================================================
 */
void MmgEmojiBox_Init(void)
{
    int i;
    if(!app || !app->emojiBoxWinObj || !app->appObj) return;

    /* Close gadget hides the window */
    DoMethod(app->emojiBoxWinObj, MUIM_Notify,
             MUIA_Window_CloseRequest, TRUE,
             app->emojiBoxWinObj, 3, MUIM_Set, MUIA_Window_Open, FALSE);

    /* Cycle active → RID_EMOJI_SET_CHANGE */
    if(app->emojiBoxCycleObj)
        DoMethod(app->emojiBoxCycleObj, MUIM_Notify,
                 MUIA_Cycle_Active, MUIV_EveryTime,
                 app->appObj, 2, MUIM_Application_ReturnID,
                 (ULONG)RID_EMOJI_SET_CHANGE);

    /* ANSI buttons: MUIA_Pressed=FALSE (mouse release) → ReturnID */
    for(i=0; i<MMG_NUM_ANSI_BTNS; i++){
        if(s_ansiBtn[i])
            DoMethod(s_ansiBtn[i], MUIM_Notify,
                     MUIA_Pressed, FALSE,
                     app->appObj, 2, MUIM_Application_ReturnID,
                     (ULONG)(RID_EMOJI_ANSI_BASE + i));
    }

    /* Get URPDrawContext from the emoji button */
    if(app->emojiBtnObj && !s_dc){
        struct URPDrawContext *dc=NULL;
        GetAttr(UBT_URPDrawContext, app->emojiBtnObj, (ULONG *)&dc);
        if(dc){ URPDC_Retain(dc); s_dc=dc; }
    }
}

/* =========================================================================
 * MmgEmojiBox_Dispose
 * =========================================================================
 */
void MmgEmojiBox_Dispose(void)
{
    if(s_dc)        { URPDC_Release(s_dc); s_dc=NULL; }
    if(s_gridClass) { FreeClass(s_gridClass); s_gridClass=NULL; }
    if(URPBase)     { CloseLibrary(URPBase); URPBase=NULL; }
}

/* =========================================================================
 * MmgEmojiBox_FlushPendingRender
 * Call from the main task (SIGBREAKF_CTRL_F handler) after draining the
 * BOOPSI queue.  If a GM_RENDER was skipped due to wrong-process context,
 * this triggers a safe RefreshGList from the correct task.
 * =========================================================================
 */
void MmgEmojiBox_FlushPendingRender(void)
{
    struct Window *emojiWin = NULL;
    Object        *rawGrid  = NULL;

    if (!s_pendingRender || !app) return;
    s_pendingRender = FALSE;

    if (!app->emojiBoxWinObj || !app->emojiBoxGridObj) return;
    GetAttr(MUIA_Window_Window,  app->emojiBoxWinObj,  (ULONG *)&emojiWin);
    GetAttr(MUIA_Boopsi_Object,  app->emojiBoxGridObj, (ULONG *)&rawGrid);
    if (emojiWin && rawGrid)
        RefreshGList((struct Gadget *)rawGrid, emojiWin, NULL, 1);
}

/* =========================================================================
 * MmgEmojiBox_RefreshGrid
 * Call after the shared DC fonts are updated (font settings change).
 * Re-sets EGRID_URPDrawContext so inst->screen is cleared (forcing font
 * metrics to be re-measured on the next GM_RENDER), then calls RefreshGList
 * if the emoji box window is currently open.
 * =========================================================================
 */
void MmgEmojiBox_RefreshGrid(void)
{
    ULONG isOpen = 0;
    Object *rawGrid = NULL;
    struct Window *emojiWin = NULL;

    if (!app || !app->emojiBoxWinObj || !app->emojiBoxGridObj) return;
    GetAttr(MUIA_Window_Open, app->emojiBoxWinObj, &isOpen);
    if (!isOpen) return;

    GetAttr(MUIA_Boopsi_Object, app->emojiBoxGridObj, (ULONG *)&rawGrid);
    if (rawGrid && s_dc)
        SetAttrs(rawGrid, EGRID_URPDrawContext, (ULONG)s_dc, TAG_DONE);

    GetAttr(MUIA_Window_Window, app->emojiBoxWinObj, (ULONG *)&emojiWin);
    if (emojiWin && rawGrid)
        RefreshGList((struct Gadget *)rawGrid, emojiWin, NULL, 1);
}

/* =========================================================================
 * MmgEmojiBox_Open / Close
 * =========================================================================
 */
void MmgEmojiBox_Open(void)
{
    if(!app || !app->emojiBoxWinObj) return;
    /* Push DC to grid on first open (screen is available at this point) */
    if(s_dc && app->emojiBoxGridObj){
        Object *rawGrid=NULL;
        GetAttr(MUIA_Boopsi_Object, app->emojiBoxGridObj, (ULONG *)&rawGrid);
        if(rawGrid) SetAttrs(rawGrid, EGRID_URPDrawContext,(ULONG)s_dc, TAG_DONE);
    }
    SetAttrs(app->emojiBoxWinObj, MUIA_Window_Open, TRUE, TAG_DONE);
}

void MmgEmojiBox_Close(void)
{
    if(app && app->emojiBoxWinObj)
    {
        struct Window  *w = NULL;
        SetAttrs(app->emojiBoxWinObj, MUIA_Window_Open, FALSE, TAG_DONE);
        /* super heavy unlock */
        if (app && app->winObj)
            GetAttr(MUIA_Window_Window, app->winObj, (ULONG *)&w);
        if(w)
        {
            SizeWindow(w,-1,-1);
            SizeWindow(w,1,1);
            ActivateWindow(w);
        }


    }
}

/* =========================================================================
 * MmgEmojiBox_HandleFKey
 * =========================================================================
 */
void MmgEmojiBox_HandleFKey(UWORD code, UWORD qualifiers)
{
    int fkey, row, idx;
    const char *emoji;
    struct Gadget *g=NULL; struct Window *w=NULL;

    if(code<0x50||code>0x59) return;
    fkey=(int)(code-0x50);
    row=0;
    if(qualifiers&(IEQUALIFIER_LSHIFT|IEQUALIFIER_RSHIFT)) row|=1;
    if(qualifiers&(IEQUALIFIER_LALT  |IEQUALIFIER_RALT))   row|=2;
    idx=row*10+fkey;
    emoji=emojiSets[s_currentSet].emojis[idx];
    if(!emoji||!emoji[0]) return;
    App_GetRawEditorWin(&g,&w);
    if(g&&w) SetGadgetAttrs(g,w,NULL,UTED_InsertText,(ULONG)emoji,TAG_DONE);
}

/* =========================================================================
 * MmgEmojiBox_HandleGridClick
 * =========================================================================
 */
void MmgEmojiBox_HandleGridClick(int cidx)
{
    const char *emoji;
    struct Gadget *g=NULL; struct Window *w=NULL;
    if(cidx<0||cidx>39) return;
    emoji=emojiSets[s_currentSet].emojis[cidx];
    if(!emoji||!emoji[0]) return;
    App_GetRawEditorWin(&g,&w);
    if(g&&w) SetGadgetAttrs(g,w,NULL,UTED_InsertText,(ULONG)emoji,TAG_DONE);
}

/* =========================================================================
 * MmgEmojiBox_HandleSetChange
 * =========================================================================
 */
void MmgEmojiBox_HandleSetChange(void)
{
    ULONG active=0;
    Object *rawGrid=NULL;
    struct Window *emojibox_win=NULL;

    if(!app||!app->emojiBoxCycleObj||!app->emojiBoxGridObj) return;
    GetAttr(MUIA_Cycle_Active, app->emojiBoxCycleObj, &active);
    if((int)active>=EMOJIBOX_NUM_SETS) return;
    s_currentSet=(int)active;


    GetAttr(MUIA_Boopsi_Object, app->emojiBoxGridObj, (ULONG *)&rawGrid);

    if (app->emojiBoxWinObj)
        GetAttr(MUIA_Window_Window,  app->emojiBoxWinObj,   (ULONG *)&emojibox_win);

    if(rawGrid && emojibox_win)
    {
        //SetAttrs(rawGrid, EGRID_EmojiSet,(ULONG)emojiSets[s_currentSet].emojis, TAG_DONE);
        SetGadgetAttrs(rawGrid,emojibox_win,NULL,
            EGRID_EmojiSet,(ULONG)emojiSets[s_currentSet].emojis, TAG_DONE
            );
        RefreshGList(rawGrid,emojibox_win,NULL,TAG_END);
    }

}

/* =========================================================================
 * MmgEmojiBox_GetAnsiString
 * =========================================================================
 */
const char *MmgEmojiBox_GetAnsiString(int idx)
{
    if(idx<0||idx>=MMG_NUM_ANSI_BTNS) return NULL;
    return s_ansiStrings[idx];
}
