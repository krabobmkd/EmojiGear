/*
 * fs3eemojibox.c - Emoji-table popup window for FriendSh3ep.
 * See fs3eemojibox.h for what changed vs. EmojiGear/emojibox.c, the
 * source this was forked from (own copy, not shared).
 *
 * The popup shows one emoji set at a time as a 4x10 grid, picked via a
 * chooser gadget in the top bar. Clicking a cell reports its index
 * through the private grid gadget's FSEB_ClickedIdx attribute;
 * FS3EEmojiBoxWindow_GetClickedUTF8() resolves that back to the actual
 * UTF-8 emoji string for the caller (friendsh3ep.c) to insert wherever
 * it likes.
 */

#include <string.h>
#include <stdio.h>

#include <exec/memory.h>
#include <exec/lists.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/alib.h>
#include <proto/utility.h>

#include <proto/layout.h>
#include <gadgets/layout.h>
#include <proto/button.h>
#include <gadgets/button.h>
#include <proto/chooser.h>
#include <gadgets/chooser.h>
#include <proto/window.h>
#include <classes/window.h>
#include <proto/label.h>
#include <images/label.h>

#include <devices/inputevent.h>
#include <intuition/gadgetclass.h>
#include <intuition/icclass.h>
#include <intuition/screens.h>

#include <libraries/utf8rastport.h>
#include <proto/utf8rastport.h>

#include <gadgets/unitexteditor.h>

#include "compilers.h"
#include "bdbprintf.h"
#include "fs3eemojibox.h"
#include "fs3eboopsimainwindow.h"
#include "fs3eboopsimessage.h"
#include "fs3egadgetid.h"

#include "friendsh3ep.h"

/* Raw key codes for F1-F10 */
#define EMOJIBOX_RAWKEY_F1   0x50
#define EMOJIBOX_RAWKEY_F8   0x57
#define EMOJIBOX_RAWKEY_F9   0x58
#define EMOJIBOX_RAWKEY_F10  0x59

extern struct Library *ChooserBase;

/* Main task pointer (friendsh3ep.c) -- used by the process guard in
 * FSEBGrid_OnRender, same reasoning as TootTimeline's render code: Intuition
 * can call GM_RENDER from a task other than the one that owns the
 * URPDrawContext, and utf8rastport is not safe to call from there. */
extern struct Task *myTask;

/* =========================================================================
 * Emoji set tables  (40 entries each, row-major: idx = row*10 + col)
 * Ported verbatim from EmojiGear/emojibox.c.
 * =========================================================================
 */

static const char *popularEmojiTable[40] = {
    "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x8A", "\xF0\x9F\x98\x8D", "\xF0\x9F\xA4\x94",
    "\xF0\x9F\x98\x8E", "\xF0\x9F\xA5\xB3", "\xF0\x9F\x98\x8B", "\xF0\x9F\x98\x8F",
    "\xF0\x9F\x98\x82", "\xF0\x9F\x98\xAA",
    "\xF0\x9F\xA4\xA9", "\xF0\x9F\x98\x85", "\xF0\x9F\x98\xB1", "\xE2\x9D\xA4",
    "\xF0\x9F\x91\x8D", "\xF0\x9F\x99\x8F", "\xF0\x9F\x91\x80", "\xF0\x9F\x9A\x80",
    "\xF0\x9F\x8E\x89", "\xF0\x9F\x92\x83",
    "\xF0\x9F\x8D\x95", "\xF0\x9F\xA5\x82", "\xF0\x9F\x92\xAA", "\xE2\x98\x95",
    "\xF0\x9F\x93\xB1", "\xF0\x9F\x92\xBB", "\xF0\x9F\x92\xBE", "\xF0\x9F\xA4\x96",
    "\xF0\x9F\xA6\x84", "\xF0\x9F\x8C\x88",
    "\xF0\x9F\x8C\xB8", "\xF0\x9F\x92\xA9", "\xF0\x9F\x8E\xB8", "\xF0\x9F\x92\x8E",
    "\xF0\x9F\x8C\x99", "\xE2\xAD\x90", "\xF0\x9F\x8E\xB5", "\xF0\x9F\x8C\x8D",
    "\xF0\x9F\x95\x99", "\xF0\x9F\x92\xA1",
};

static const char *urbanEmojiTable[40] = {
    "\xF0\x9F\x9A\x97", "\xF0\x9F\x9A\x95", "\xF0\x9F\x9A\x8C", "\xF0\x9F\x9A\x8E",
    "\xF0\x9F\x8F\x8E", "\xF0\x9F\x9A\x93", "\xF0\x9F\x9A\x91", "\xF0\x9F\x9A\x92",
    "\xF0\x9F\x9A\x90", "\xF0\x9F\x9A\x9A",
    "\xF0\x9F\x9A\x82", "\xE2\x9C\x88", "\xF0\x9F\x9A\x81", "\xF0\x9F\x9B\xB8",
    "\xF0\x9F\x9A\xB2", "\xF0\x9F\x9B\xB4", "\xF0\x9F\x9B\xB5", "\xF0\x9F\x8F\x8D",
    "\xF0\x9F\x9A\x80", "\xF0\x9F\x9B\xBB",
    "\xF0\x9F\x8F\x99", "\xF0\x9F\x8F\xA2", "\xF0\x9F\x8F\xAC", "\xF0\x9F\x8F\xA6",
    "\xF0\x9F\x8F\xA8", "\xF0\x9F\x8F\xA9", "\xF0\x9F\x8F\xAA", "\xF0\x9F\x8F\xAB",
    "\xF0\x9F\x8F\x9B", "\xF0\x9F\x95\x8C",
    "\xF0\x9F\x91\xB7", "\xF0\x9F\x91\xAE", "\xF0\x9F\x92\xBC", "\xF0\x9F\x94\xA7",
    "\xF0\x9F\x94\xA8", "\xF0\x9F\x8F\x97", "\xF0\x9F\x9A\xA7", "\xF0\x9F\x97\xBA",
    "\xF0\x9F\x93\xAB", "\xF0\x9F\x9A\x89",
};

static const char *natureEmojiTable[40] = {
    "\xF0\x9F\x8C\xB8", "\xF0\x9F\x8C\xBA", "\xF0\x9F\x8C\xBB", "\xF0\x9F\x8C\xB9",
    "\xF0\x9F\x8C\xB7", "\xF0\x9F\x8C\xBC", "\xF0\x9F\x8C\xBF", "\xF0\x9F\x8D\x80",
    "\xF0\x9F\x8C\xB1", "\xF0\x9F\x8C\xBE",
    "\xF0\x9F\x8D\x84", "\xF0\x9F\x8C\xB5", "\xF0\x9F\x8C\xB4", "\xF0\x9F\x8C\xB2",
    "\xF0\x9F\x8C\xB3", "\xF0\x9F\x8E\x8D", "\xF0\x9F\x8D\x81", "\xF0\x9F\x8D\x8C",
    "\xF0\x9F\xA5\x95", "\xF0\x9F\xA5\xA6",
    "\xF0\x9F\x8C\xBD", "\xF0\x9F\x8D\x85", "\xF0\x9F\xA5\x91", "\xF0\x9F\x8D\x86",
    "\xF0\x9F\xA5\x94", "\xF0\x9F\xA7\x85", "\xF0\x9F\xA7\x84", "\xF0\x9F\x8C\xB6",
    "\xE2\x9B\xB0", "\xF0\x9F\x8F\x94",
    "\xF0\x9F\x97\xBB", "\xF0\x9F\x8C\x8B", "\xF0\x9F\x8F\x9D", "\xF0\x9F\x8F\x9C",
    "\xF0\x9F\x8C\x8A", "\xE2\x98\x80", "\xF0\x9F\x8C\x9E", "\xF0\x9F\x8C\x85",
    "\xF0\x9F\x8C\x84", "\xF0\x9F\x8C\x8C",
};

static const char *symbolsEmojiTable[40] = {
    "$", "\xE2\x82\xAC", "\xC2\xA3", "\xC2\xA5",
    "\xE2\x82\xA3", "\xE2\x82\xA4", "\xE2\x82\xA6", "\xE2\x82\xA9",
    "\xE2\x82\xAA", "\xE2\x82\xAB",
    "\xE2\x86\x92", "\xE2\x86\x90", "\xE2\x86\x91", "\xE2\x86\x93",
    "\xE2\x86\x94", "\xE2\x86\x95", "\xE2\x87\x92", "\xE2\x87\x90",
    "\xE2\x87\x91", "\xE2\x87\x93",
    "\xE2\x9C\x93", "\xE2\x9C\x97", "\xE2\x98\x85", "\xE2\x98\x86",
    "\xE2\x99\xA5", "\xE2\x99\xA6", "\xE2\x99\xA0", "\xE2\x99\xA3",
    "\xC2\xA9", "\xC2\xAE",
    "\xE2\x84\xA2", "\xC2\xB0", "\xC2\xA7", "\xC2\xB6",
    "\xE2\x80\xA0", "\xE2\x80\xA1", "\xE2\x80\xA2", "\xE2\x80\xA6",
    "\xE2\x88\x9E", "\xE2\x89\xA0",
};

static const char *sportsEmojiTable[40] = {
    "\xE2\x9A\xBD", "\xF0\x9F\x8F\x80", "\xF0\x9F\x8F\x88", "\xE2\x9A\xBE",
    "\xF0\x9F\x8E\xBE", "\xF0\x9F\x8F\x90", "\xF0\x9F\x8F\x89", "\xF0\x9F\x8E\xB1",
    "\xF0\x9F\x8F\x93", "\xF0\x9F\x8F\xB8",
    "\xF0\x9F\xA5\x8A", "\xF0\x9F\xA5\x8B", "\xF0\x9F\xA4\xBC", "\xF0\x9F\xA4\xB8",
    "\xF0\x9F\x8F\x8A", "\xF0\x9F\x9A\xB4", "\xF0\x9F\x8F\x83", "\xF0\x9F\xA7\x97",
    "\xF0\x9F\xA4\xBA", "\xE2\x9B\xB7",
    "\xF0\x9F\x8F\x86", "\xF0\x9F\xA5\x87", "\xF0\x9F\xA5\x88", "\xF0\x9F\xA5\x89",
    "\xF0\x9F\x8F\x85", "\xF0\x9F\x8E\xAF", "\xF0\x9F\x8E\xB3", "\xF0\x9F\x8F\x8B",
    "\xF0\x9F\xA4\xBE", "\xF0\x9F\x8F\x84",
    "\xF0\x9F\xA7\x98", "\xF0\x9F\x8F\x82", "\xF0\x9F\xA4\xBF", "\xF0\x9F\x8E\xBF",
    "\xE2\x9B\xB8", "\xF0\x9F\x8F\x8C", "\xF0\x9F\x8F\xB9", "\xF0\x9F\xA5\x85",
    "\xF0\x9F\x8F\x91", "\xF0\x9F\x8F\x92",
};

static const char *katakanaTable[40] = {
    "\xE3\x82\xA2", "\xE3\x82\xA4", "\xE3\x82\xA6", "\xE3\x82\xA8",
    "\xE3\x82\xAA", "\xE3\x82\xAB", "\xE3\x82\xAD", "\xE3\x82\xAF",
    "\xE3\x82\xB1", "\xE3\x82\xB3",
    "\xE3\x82\xB5", "\xE3\x82\xB7", "\xE3\x82\xB9", "\xE3\x82\xBB",
    "\xE3\x82\xBD", "\xE3\x82\xBF", "\xE3\x83\x81", "\xE3\x83\x84",
    "\xE3\x83\x86", "\xE3\x83\x88",
    "\xE3\x83\x8A", "\xE3\x83\x8B", "\xE3\x83\x8C", "\xE3\x83\x8D",
    "\xE3\x83\x8E", "\xE3\x83\x8F", "\xE3\x83\x92", "\xE3\x83\x95",
    "\xE3\x83\x98", "\xE3\x83\x9B",
    "\xE3\x83\x9E", "\xE3\x83\x9F", "\xE3\x83\xA0", "\xE3\x83\xA1",
    "\xE3\x83\xA2", "\xE3\x83\xA4", "\xE3\x83\xA6", "\xE3\x83\xA8",
    "\xE3\x83\xA9", "\xE3\x83\xAA",
};

static const char *cookingEmojiTable[40] = {
    "\xF0\x9F\x8D\xB3", "\xF0\x9F\xA5\x98", "\xF0\x9F\xAB\x95", "\xF0\x9F\x8D\xB2",
    "\xF0\x9F\xA5\x97", "\xF0\x9F\xAB\x99", "\xF0\x9F\xA7\x82", "\xF0\x9F\xA7\x88",
    "\xF0\x9F\xAB\x92", "\xF0\x9F\x8D\xB1",
    "\xF0\x9F\xA5\xA9", "\xF0\x9F\x8D\x97", "\xF0\x9F\x8D\x96", "\xF0\x9F\xA5\x9A",
    "\xF0\x9F\xA7\x80", "\xF0\x9F\x8D\xA3", "\xF0\x9F\x8D\xA4", "\xF0\x9F\xA6\x90",
    "\xF0\x9F\xA6\x91", "\xF0\x9F\xA5\x93",
    "\xF0\x9F\x8D\x9E", "\xF0\x9F\xA5\x90", "\xF0\x9F\xA5\x96", "\xF0\x9F\xA7\x81",
    "\xF0\x9F\x8E\x82", "\xF0\x9F\x8D\xB0", "\xF0\x9F\x8D\xA9", "\xF0\x9F\x8D\xAA",
    "\xF0\x9F\x8D\xAB", "\xF0\x9F\x8D\xAD",
    "\xF0\x9F\x8D\x87", "\xF0\x9F\x8D\x93", "\xF0\x9F\x8D\x8E", "\xF0\x9F\x8D\x8A",
    "\xF0\x9F\x8D\x8B", "\xE2\x98\x95", "\xF0\x9F\x8D\xB5", "\xF0\x9F\xA7\x83",
    "\xF0\x9F\x8D\xBA", "\xF0\x9F\x8D\xB7",
};

static const char *cultureEmojiTable[40] = {
    "\xF0\x9F\x8E\xAD", "\xF0\x9F\x8E\xAC", "\xF0\x9F\x8E\xA4", "\xF0\x9F\x8E\xA7",
    "\xF0\x9F\x8E\xB9", "\xF0\x9F\x8E\xB8", "\xF0\x9F\x8E\xBA", "\xF0\x9F\xA5\x81",
    "\xF0\x9F\x8E\xBB", "\xF0\x9F\xAA\x95",
    "\xF0\x9F\x8E\xA8", "\xF0\x9F\x96\x8C", "\xF0\x9F\x93\x9A", "\xF0\x9F\x93\x96",
    "\xF0\x9F\x97\xBF", "\xF0\x9F\x8F\xBA", "\xF0\x9F\x96\xBC", "\xF0\x9F\x8E\xAA",
    "\xF0\x9F\x8E\xA1", "\xF0\x9F\x8E\xA2",
    "\xF0\x9F\x8F\xB0", "\xF0\x9F\x97\xBC", "\xF0\x9F\x97\xBD", "\xE2\x9B\xA9",
    "\xF0\x9F\x95\x8D", "\xF0\x9F\x8F\xAF", "\xF0\x9F\x8C\x89", "\xF0\x9F\x8E\x87",
    "\xF0\x9F\x8E\x86", "\xF0\x9F\x8E\x91",
    "\xE2\x99\x9F", "\xF0\x9F\x8E\xB2", "\xF0\x9F\x83\x8F", "\xF0\x9F\xA7\xA9",
    "\xF0\x9F\x8E\xB0", "\xF0\x9F\x8E\x8A", "\xF0\x9F\x8E\x89", "\xF0\x9F\x8E\x83",
    "\xF0\x9F\x8E\x84", "\xF0\x9F\xA7\xA7",
};

static const char *yearEventsTable[40] = {
    "\xF0\x9F\x8E\x84", "\xF0\x9F\x8E\x85", "\xF0\x9F\xA4\xB6", "\xF0\x9F\x8E\x81",
    "\xE2\x9B\x84", "\xE2\x9D\x84", "\xF0\x9F\x8C\x9F", "\xF0\x9F\x95\xAF",
    "\xF0\x9F\xA7\xA6", "\xF0\x9F\x94\x94",
    "\xF0\x9F\x8E\x86", "\xF0\x9F\x8E\x8A", "\xF0\x9F\x8E\x89", "\xF0\x9F\xA5\x82",
    "\xF0\x9F\x8D\xBE", "\xE2\x9D\xA4", "\xF0\x9F\x92\x9D", "\xF0\x9F\x92\x8C",
    "\xF0\x9F\x8C\xB9", "\xF0\x9F\x92\x98",
    "\xF0\x9F\x90\xA3", "\xF0\x9F\x90\xB0", "\xF0\x9F\xA5\x9A", "\xF0\x9F\x8C\xB7",
    "\xF0\x9F\x8C\xB8", "\xE2\x98\x98", "\xF0\x9F\x94\xA5", "\xF0\x9F\xA6\x8B",
    "\xF0\x9F\x90\x9D", "\xF0\x9F\x8C\xBA",
    "\xF0\x9F\x8E\x83", "\xF0\x9F\x91\xBB", "\xF0\x9F\xA6\x87", "\xF0\x9F\x95\xB7",
    "\xF0\x9F\x8D\x82", "\xF0\x9F\x8D\x81", "\xF0\x9F\xA6\x83", "\xF0\x9F\xAA\x94",
    "\xF0\x9F\x95\x8E", "\xF0\x9F\x8E\x91",
};

static const char *businessEmojiTable[40] = {
    "\xF0\x9F\x92\xBB", "\xF0\x9F\x96\xA5", "\xF0\x9F\x96\xA8", "\xE2\x8C\xA8",
    "\xF0\x9F\x96\xB1", "\xF0\x9F\x92\xBE", "\xF0\x9F\x92\xBF", "\xF0\x9F\x93\xB1",
    "\xF0\x9F\x96\xB2", "\xF0\x9F\x93\xA1",
    "\xF0\x9F\x8C\x90", "\xF0\x9F\x93\xB6", "\xF0\x9F\x94\x92", "\xF0\x9F\x94\x93",
    "\xF0\x9F\x94\x91", "\xE2\x9A\x99", "\xF0\x9F\x9B\xA0", "\xF0\x9F\xA4\x96",
    "\xF0\x9F\xA7\xA0", "\xF0\x9F\x92\xA1",
    "\xF0\x9F\x93\x8A", "\xF0\x9F\x93\x88", "\xF0\x9F\x93\x89", "\xF0\x9F\x92\xB0",
    "\xF0\x9F\x92\xB3", "\xF0\x9F\x92\xB5", "\xF0\x9F\x93\x8B", "\xF0\x9F\x93\x81",
    "\xF0\x9F\x97\x82", "\xF0\x9F\x8F\xA6",
    "\xF0\x9F\x91\x94", "\xF0\x9F\x92\xBC", "\xF0\x9F\x93\xA7", "\xF0\x9F\x93\xA8",
    "\xF0\x9F\x93\x85", "\xE2\x9C\x8F", "\xF0\x9F\x93\x9D", "\xF0\x9F\x96\x8A",
    "\xF0\x9F\x97\x92", "\xF0\x9F\x93\x8C",
};

static const char *animalsEmojiTable[40] = {
    "\xF0\x9F\x90\xB6", "\xF0\x9F\x90\xB1", "\xF0\x9F\x90\xAD", "\xF0\x9F\x90\xB0",
    "\xF0\x9F\x90\x91", "\xF0\x9F\xA6\x8A", "\xF0\x9F\x90\xBB", "\xF0\x9F\x90\xBC",
    "\xF0\x9F\x90\xA8", "\xF0\x9F\x90\xAF", "\xF0\x9F\xA6\x81",
    "\xF0\x9F\x90\xAE", "\xF0\x9F\x90\xB7", "\xF0\x9F\x90\xB8", "\xF0\x9F\x90\xB5",
    "\xF0\x9F\x99\x88", "\xF0\x9F\x90\x94", "\xF0\x9F\x90\xA7", "\xF0\x9F\x90\xA6",
    "\xF0\x9F\xA6\x86", "\xF0\x9F\xA6\x85",
    "\xF0\x9F\x90\xAC", "\xF0\x9F\x90\xB3", "\xF0\x9F\xA6\x88", "\xF0\x9F\x90\xA0",
    "\xF0\x9F\x90\x99", "\xF0\x9F\xA6\x80", "\xF0\x9F\xA6\x8B", "\xF0\x9F\x90\x9D",
    "\xF0\x9F\x90\x9B",
    "\xF0\x9F\x90\x8A", "\xF0\x9F\x90\xA2", "\xF0\x9F\xA6\x8E", "\xF0\x9F\x90\x8D",
    "\xF0\x9F\xA6\x93", "\xF0\x9F\xA6\x92", "\xF0\x9F\x90\x98", "\xF0\x9F\xA6\x8F",
    "\xF0\x9F\xA6\x94", "\xF0\x9F\xA6\x98",
};

static const char *diacriticsTable[40] = {
    "\xC2\xB7", "\xC3\xA6", "\xC5\x93", "\xC3\x86",
    "\xC5\x92", "\xC3\xBF", "\xC5\xB8", "\xC2\xAB",
    "\xC2\xBB", "\xE2\x80\x99",
    "\xC3\x89", "\xC3\x88", "\xC3\x8A", "\xC3\x8B",
    "\xC3\x80", "\xC3\x82", "\xC3\x8E", "\xC3\x8F",
    "\xC3\x94", "\xC3\x9B",
    "\xC3\x99", "\xC3\x87", "\xC3\xA4", "\xC3\xB6",
    "\xC3\xBC", "\xC3\x9F", "\xC3\x84", "\xC3\x96",
    "\xC3\x9C", "\xC3\xA5",
    "\xC3\x85", "\xC3\xB8", "\xC3\x98", "\xC3\xB1",
    "\xC3\x91", "\xC5\xA1", "\xC5\xBE", "\xC4\x8D",
    "\xEF\xAC\x81", "\xEF\xAC\x82",
};

typedef struct {
    const char  *name;
    const char **emojis; /* points to a [40] array */
} FSEBSetDesc;

static const FSEBSetDesc emojiSets[FS3EEMOJIBOX_NUM_SETS] = {
    { "Popular Emojis", popularEmojiTable  },
    { "Urban Emojis",   urbanEmojiTable    },
    { "Nature Emojis",  natureEmojiTable   },
    { "Symbols",        symbolsEmojiTable  },
    { "Sports",         sportsEmojiTable   },
    { "Katakana",       katakanaTable      },
    { "Cooking",        cookingEmojiTable  },
    { "Culture",        cultureEmojiTable  },
    { "Year Events",    yearEventsTable    },
    { "Business",       businessEmojiTable },
    { "Animals",        animalsEmojiTable  },
    { "Diacritics",     diacriticsTable    },
};

/* =========================================================================
 * Private emoji-grid BOOPSI gadget
 *
 * Renders the 10x4 table (headers + 40 emoji cells) and reports which
 * cell was clicked via FSEB_ClickedIdx so the window handler can resolve
 * and hand off the emoji -- see FS3EEmojiBoxWindow_GetClickedUTF8.
 * =========================================================================
 */

#define FSEB_HDR_COL_MIN   72
#define FSEB_HDR_ROW_MIN   14
#define FSEB_CELL_MIN_W    22
#define FSEB_CELL_MIN_H    24
#define FSEB_MIN_GAD_W  (FSEB_HDR_COL_MIN + 10 * FSEB_CELL_MIN_W)
#define FSEB_MIN_GAD_H  (FSEB_HDR_ROW_MIN +  4 * FSEB_CELL_MIN_H)
#define FSEBW_EXTRA_H    60      /* top bar + chrome + spacing */
#define FSEBW_OPEN_W     400
#define FSEBW_OPEN_H     220
#define FSEBW_MIN_W  (FSEB_MIN_GAD_W + 8)
#define FSEBW_MIN_H  (FSEB_MIN_GAD_H + FSEBW_EXTRA_H)

/* Private tag base */
#define FSEB_Dummy      (TAG_USER | 0x7480)
#define FSEB_EmojiSet   (FSEB_Dummy + 1)  /* [IS] const char *[40] */
#define FSEB_ClickedIdx (FSEB_Dummy + 2)  /* [G]  last clicked idx (0-39), -1 = none */

typedef struct {
    const char           **emojis;     /* pointer to 40-entry set */
    struct URPDrawContext *dc;         /* emoji rendering context (borrowed) */
    int                    clickedIdx; /* -1 = none */
    /* Layout metrics filled in GM_RENDER */
    WORD  headerColW;
    WORD  headerRowH;
    WORD  cellW;
    WORD  cellH;
    WORD  fontHeight;
    WORD  fontAscent;
    struct Screen *screen;
    UWORD pens[12]; /* cached copy of dri_Pens, indexed by standard pen constants */
} FSEBGridInst;

#define FSEBPEN_BG     BACKGROUNDPEN
#define FSEBPEN_FILL   FILLPEN
#define FSEBPEN_SHINE  SHINEPEN
#define FSEBPEN_SHADOW SHADOWPEN
#define FSEBPEN_TEXT   TEXTPEN

#define FSEB_INST(cl,o) ((FSEBGridInst *)INST_DATA((cl),(o)))
#define FSEB_G(o)       ((struct Gadget *)(o))

/* Set when GM_RENDER is called from the wrong process context.
 * Cleared by FS3EEmojiBoxWindow_FlushPendingRender() from the main task. */
static volatile BOOL s_pendingGridRender = FALSE;

/* Purely visual grid coordinates -- unlike EmojiGear's original, there is
 * no F-key shortcut feature behind these labels (see fs3eemojibox.h), so
 * they're plain column/row numbers rather than "F1".."F10"/"Shift"/"Ctrl"
 * captions that would wrongly imply a keyboard shortcut exists. */
static const char *colLabels[10] = {
    "1","2","3","4","5","6","7","8","9","10"
};
static const char *rowLabels[4] = {
    "-", "Shift", "Ctrl", "Sh+Ctrl"
};

/* -------------------------------------------------------------------------
 * GM_RENDER
 * -------------------------------------------------------------------------*/
static ULONG FSEBGrid_OnRender(Class *cl, Object *o, struct gpRender *msg)
{
    FSEBGridInst *inst = FSEB_INST(cl, o);
    struct RastPort *rp  = msg->gpr_RPort;
    struct Gadget   *g   = FSEB_G(o);
    struct Screen   *scr = msg->gpr_GInfo ? msg->gpr_GInfo->gi_Screen : NULL;
    struct DrawInfo *dri = msg->gpr_GInfo ? msg->gpr_GInfo->gi_DrInfo : NULL;
    WORD gx = g->LeftEdge, gy = g->TopEdge;
    WORD gw = g->Width,    gh = g->Height;
    WORD col, row;
    WORD fontH, baseLine;

    /* utf8rastport must run in the correct process context. If Intuition
     * calls us from a different task, defer and wake the main task. */
    if (FindTask(NULL) != myTask) {
        s_pendingGridRender = TRUE;
        if (myTask) Signal(myTask, SIGBREAKF_CTRL_F);
        return 0;
    }

    if (!rp || gw <= 0 || gh <= 0) return 0;

    if (dri) {
        inst->pens[FSEBPEN_BG]     = dri->dri_Pens[BACKGROUNDPEN];
        inst->pens[FSEBPEN_FILL]   = dri->dri_Pens[FILLPEN];
        inst->pens[FSEBPEN_SHINE]  = dri->dri_Pens[SHINEPEN];
        inst->pens[FSEBPEN_SHADOW] = dri->dri_Pens[SHADOWPEN];
        inst->pens[FSEBPEN_TEXT]   = dri->dri_Pens[TEXTPEN];
    }

    if (scr && inst->dc && scr != inst->screen) {
        struct URPTextMetric m;
        inst->screen = scr;

        URPDC_SetDrawScreen(inst->dc, scr);

        URPDC_GetFontLineMetrics(inst->dc, &m);
        if (m.height <= 0)
            URPDC_TextSizeUTF8(inst->dc, "Agpqj", -1, &m);

        inst->fontHeight = m.height > 0 ? m.height : 14;
        inst->fontAscent = m.baseY  > 0 ? m.baseY  : inst->fontHeight;
    }

    fontH    = rp->Font ? (WORD)rp->Font->tf_YSize    : 8;
    baseLine = rp->Font ? (WORD)rp->Font->tf_Baseline : 6;

    inst->headerRowH = fontH + 6;
    {
        WORD maxLblW = 0, ri;
        for (ri = 0; ri < 4; ri++) {
            WORD tw = (WORD)TextLength(rp, rowLabels[ri],
                                       (ULONG)strlen(rowLabels[ri]));
            if (tw > maxLblW) maxLblW = tw;
        }
        inst->headerColW = maxLblW + 14;
        if (inst->headerColW < FSEB_HDR_COL_MIN)
            inst->headerColW = FSEB_HDR_COL_MIN;
    }
    {
        WORD cw = gw - inst->headerColW;
        WORD ch = gh - inst->headerRowH;
        inst->cellW = (cw > 0) ? cw / 10 : 0;
        inst->cellH = (ch > 0) ? ch / 4  : 0;
    }

    SetAPen(rp, inst->pens[FSEBPEN_BG]);
    SetDrMd(rp, JAM1);
    RectFill(rp, (LONG)gx, (LONG)gy,
             (LONG)(gx + gw - 1), (LONG)(gy + gh - 1));

    if (inst->cellW <= 0 || inst->cellH <= 0) return 0;

    /* ---- Header row: F1..F10 column labels ---- */
    for (col = 0; col < 10; col++) {
        WORD cx   = gx + inst->headerColW + col * inst->cellW;
        WORD cw   = inst->cellW;
        WORD ch   = inst->headerRowH;
        const char *lbl = colLabels[col];
        WORD tw, tx, ty;

        SetAPen(rp, inst->pens[FSEBPEN_SHINE]);
        SetDrMd(rp, JAM1);
        RectFill(rp, (LONG)cx, (LONG)gy,
                 (LONG)(cx + cw - 1), (LONG)(gy + ch - 1));

        tw = (WORD)TextLength(rp, lbl, (ULONG)strlen(lbl));
        tx = cx + (cw - tw) / 2;
        ty = gy + (ch - fontH) / 2 + baseLine;
        SetAPen(rp, inst->pens[FSEBPEN_SHADOW]);
        SetBPen(rp, inst->pens[FSEBPEN_SHINE]);
        SetDrMd(rp, JAM2);
        Move(rp, (LONG)tx, (LONG)ty);
        Text(rp, lbl, (ULONG)strlen(lbl));
    }

    /* ---- Header column: row modifier labels ---- */
    for (row = 0; row < 4; row++) {
        WORD rx   = gx;
        WORD ry   = gy + inst->headerRowH + row * inst->cellH;
        WORD rw   = inst->headerColW;
        WORD rh   = inst->cellH;
        const char *lbl = rowLabels[row];
        WORD tw, tx, ty;

        SetAPen(rp, inst->pens[FSEBPEN_SHINE]);
        SetDrMd(rp, JAM1);
        RectFill(rp, (LONG)rx, (LONG)ry,
                 (LONG)(rx + rw - 1), (LONG)(ry + rh - 1));

        tw = (WORD)TextLength(rp, lbl, (ULONG)strlen(lbl));
        tx = rx + (rw - tw) / 2;
        ty = ry + (rh - fontH) / 2 + baseLine;
        SetAPen(rp, inst->pens[FSEBPEN_SHADOW]);
        SetBPen(rp, inst->pens[FSEBPEN_SHINE]);
        SetDrMd(rp, JAM2);
        Move(rp, (LONG)tx, (LONG)ty);
        Text(rp, lbl, (ULONG)strlen(lbl));
    }

    /* ---- Corner cell (top-left) ---- */
    SetAPen(rp, inst->pens[FSEBPEN_SHINE]);
    SetDrMd(rp, JAM1);
    RectFill(rp, (LONG)gx, (LONG)gy,
             (LONG)(gx + inst->headerColW - 1),
             (LONG)(gy + inst->headerRowH - 1));

    /* ---- Emoji cells ---- */
    if (inst->emojis && inst->dc && inst->screen) {
        URPDC_UpdateColorMap(inst->dc, inst->screen);
        URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                  (LONG)inst->pens[FSEBPEN_TEXT], (LONG)inst->pens[FSEBPEN_BG]);
        SetAPen(rp, inst->pens[FSEBPEN_TEXT]);
        SetBPen(rp, inst->pens[FSEBPEN_BG]);
        SetDrMd(rp, JAM2);

        for (row = 0; row < 4; row++) {
            for (col = 0; col < 10; col++) {
                int idx = row * 10 + col;
                const char *emoji = inst->emojis[idx];
                WORD cx, cy;
                struct URPTextMetric m;
                struct URPTextPos pos;

                if (!emoji || !emoji[0]) continue;

                cx = gx + inst->headerColW + col * inst->cellW;
                cy = gy + inst->headerRowH + row * inst->cellH;

                URPDC_TextSizeUTF8(inst->dc, emoji, -1, &m);
                pos.x = (WORD)(cx + (inst->cellW  - m.width)      / 2);
                pos.y = (WORD)(cy + (inst->cellH  - inst->fontHeight) / 2
                               + inst->fontAscent);
                if (pos.x < cx)                     pos.x = cx;
                if (pos.y < cy + inst->fontAscent)  pos.y = cy + inst->fontAscent;
                URPDC_SetDrawColorFromPen(inst->dc, inst->screen,
                                  (LONG)inst->pens[FSEBPEN_TEXT], (LONG)inst->pens[FSEBPEN_BG]);

                URPDrawTextUTF8(rp, inst->dc, &pos, emoji, (ULONG)(-1));
            }
        }
    }

    /* ---- Grid lines (shadow pen) ---- */
    {
        WORD gridRight  = gx + inst->headerColW + 10 * inst->cellW;
        WORD gridBottom = gy + inst->headerRowH + 4  * inst->cellH;

        SetAPen(rp, inst->pens[FSEBPEN_SHADOW]);
        SetDrMd(rp, JAM1);

        Move(rp, (LONG)gx,        (LONG)gy);
        Draw(rp, (LONG)gridRight,  (LONG)gy);
        Draw(rp, (LONG)gridRight,  (LONG)gridBottom);
        Draw(rp, (LONG)gx,         (LONG)gridBottom);
        Draw(rp, (LONG)gx,         (LONG)gy);

        Move(rp, (LONG)(gx + inst->headerColW), (LONG)gy);
        Draw(rp, (LONG)(gx + inst->headerColW), (LONG)gridBottom);

        Move(rp, (LONG)gx,        (LONG)(gy + inst->headerRowH));
        Draw(rp, (LONG)gridRight,  (LONG)(gy + inst->headerRowH));

        for (col = 1; col < 10; col++) {
            WORD lx = gx + inst->headerColW + col * inst->cellW;
            Move(rp, (LONG)lx, (LONG)(gy + inst->headerRowH));
            Draw(rp, (LONG)lx, (LONG)gridBottom);
        }
        for (row = 1; row < 4; row++) {
            WORD ly = gy + inst->headerRowH + row * inst->cellH;
            Move(rp, (LONG)gx,       (LONG)ly);
            Draw(rp, (LONG)gridRight, (LONG)ly);
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * GM_GOACTIVE - store clicked cell, go active for GADGETUP
 * -------------------------------------------------------------------------*/
static ULONG FSEBGrid_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    FSEBGridInst *inst = FSEB_INST(cl, o);
    WORD mx, my, col, row;

    if (!msg->gpi_IEvent) return GMR_NOREUSE;

    mx = msg->gpi_Mouse.X;
    my = msg->gpi_Mouse.Y;

    if (inst->cellW <= 0 || inst->cellH <= 0)  return GMR_NOREUSE;
    if (mx < inst->headerColW)                 return GMR_NOREUSE;
    if (my < inst->headerRowH)                 return GMR_NOREUSE;

    col = (WORD)((mx - inst->headerColW) / inst->cellW);
    row = (WORD)((my - inst->headerRowH) / inst->cellH);
    if (col > 9) col = 9;
    if (row > 3) row = 3;

    inst->clickedIdx = (int)(row * 10 + col);
    *msg->gpi_Termination = 0;
    return GMR_MEACTIVE;
}

/* -------------------------------------------------------------------------
 * GM_HANDLEINPUT - release triggers GADGETUP
 * -------------------------------------------------------------------------*/
static ULONG FSEBGrid_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    struct InputEvent *ie = msg->gpi_IEvent;
    (void)cl; (void)o;
    if (!ie) return GMR_MEACTIVE;
    if (ie->ie_Class == IECLASS_RAWMOUSE) {
        if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
            *msg->gpi_Termination = 0;
            return GMR_NOREUSE | GMR_VERIFY;
        }
        if (ie->ie_Code == IECODE_RBUTTON)
            return GMR_REUSE;
    }
    return GMR_MEACTIVE;
}

static ULONG FSEBGrid_OnGoInactive(Class *cl, Object *o,
                                    struct gpGoInactive *msg)
{
    (void)cl; (void)o; (void)msg;
    return 0;
}

static ULONG FSEBGrid_OnNew(Class *cl, Object *o, struct opSet *msg)
{
    FSEBGridInst *inst;
    Object *newObj;
    struct TagItem *ptag;

    newObj = (Object *)DoSuperMethodA(cl, o, (APTR)msg);
    if (!newObj) return 0;

    inst = FSEB_INST(cl, newObj);
    memset(inst, 0, sizeof(FSEBGridInst));
    inst->clickedIdx = -1;

    ptag = FindTagItem(FSEB_EmojiSet, msg->ops_AttrList);
    if (ptag) inst->emojis = (const char **)ptag->ti_Data;

    return (ULONG)newObj;
}

static ULONG FSEBGrid_OnDispose(Class *cl, Object *o, Msg msg)
{
    return DoSuperMethodA(cl, o, (APTR)msg);
}

static ULONG FSEBGrid_OnSet(Class *cl, Object *o, struct opSet *msg)
{
    FSEBGridInst *inst = FSEB_INST(cl, o);
    struct TagItem *state = msg->ops_AttrList;
    struct TagItem *tag;
    BOOL redraw = FALSE;

    while ((tag = NextTagItem(&state)) != NULL) {
        switch (tag->ti_Tag) {
        case FSEB_EmojiSet:
            inst->emojis = (const char **)tag->ti_Data;
            redraw = TRUE;
            break;
        default:
            break;
        }
    }

    if (redraw && msg->ops_GInfo) {
        struct RastPort *rp = ObtainGIRPort(msg->ops_GInfo);
        if (rp) {
            DoMethod(o, GM_RENDER, msg->ops_GInfo, rp, GREDRAW_REDRAW);
            ReleaseGIRPort(rp);
        }
    }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

static ULONG FSEBGrid_OnGet(Class *cl, Object *o, struct opGet *msg)
{
    FSEBGridInst *inst = FSEB_INST(cl, o);
    if (msg->opg_AttrID == FSEB_ClickedIdx) {
        *msg->opg_Storage = (ULONG)inst->clickedIdx;
        return TRUE;
    }
    return DoSuperMethodA(cl, o, (APTR)msg);
}

static ULONG FSEBGrid_OnDomain(Class *cl, Object *o, struct gpDomain *msg)
{
    FSEBGridInst *inst    = FSEB_INST(cl, o);
    struct IBox   *domain  = &msg->gpd_Domain;

    WORD hColW   = (inst->headerColW > 0) ? inst->headerColW : FSEB_HDR_COL_MIN;
    WORD hRowH   = (inst->headerRowH > 0) ? inst->headerRowH : FSEB_HDR_ROW_MIN;
    WORD emojiH  = (inst->fontHeight  > 0) ? inst->fontHeight : FSEB_CELL_MIN_H;
    WORD cellHMin = (emojiH + 4 > FSEB_CELL_MIN_H) ? emojiH + 4 : FSEB_CELL_MIN_H;

    domain->Left = 0;
    domain->Top  = 0;

    switch (msg->gpd_Which) {
    case GDOMAIN_MINIMUM:
        domain->Width  = hColW + 10 * FSEB_CELL_MIN_W;
        domain->Height = hRowH +  4 * cellHMin;
        break;
    case GDOMAIN_MAXIMUM:
        domain->Width  = 32767;
        domain->Height = 32767;
        break;
    case GDOMAIN_NOMINAL:
    default:
        domain->Width  = hColW + 10 * (FSEB_CELL_MIN_W * 3);
        domain->Height = hRowH +  4 * (cellHMin * 2);
        break;
    }
    return 1;
}

static ULONG ASM SAVEDS FSEBGrid_Dispatch(
    REG(a0, Class *cl), REG(a2, Object *o), REG(a1, Msg msg))
{
    switch (msg->MethodID) {
    case OM_NEW:         return FSEBGrid_OnNew(cl, o, (struct opSet *)msg);
    case OM_DISPOSE:     return FSEBGrid_OnDispose(cl, o, msg);
    case OM_SET:
    case OM_UPDATE:      return FSEBGrid_OnSet(cl, o, (struct opSet *)msg);
    case OM_GET:         return FSEBGrid_OnGet(cl, o, (struct opGet *)msg);
    case GM_HITTEST:     return GMR_GADGETHIT;
    case GM_DOMAIN:      return FSEBGrid_OnDomain(cl, o, (struct gpDomain *)msg);
    case GM_RENDER:      return FSEBGrid_OnRender(cl, o, (struct gpRender *)msg);
    case GM_GOACTIVE:    return FSEBGrid_OnGoActive(cl, o, (struct gpInput *)msg);
    case GM_HANDLEINPUT: return FSEBGrid_OnHandleInput(cl, o, (struct gpInput *)msg);
    case GM_GOINACTIVE:  return FSEBGrid_OnGoInactive(cl, o, (struct gpGoInactive *)msg);
    default:             return DoSuperMethodA(cl, o, (APTR)msg);
    }
}

/* =========================================================================
 * FS3EEmojiBoxWindow - window management
 * =========================================================================
 */

BOOL FS3EEmojiBoxWindow_Create(FS3EEmojiBoxWindow *ebw, struct URPDrawContext *dc)
{
    Object *topBar, *setLabel;
    int i;

    {
        LONG sl = ebw->left, st = ebw->top, sw = ebw->width, sh = ebw->height;
        memset(ebw, 0, sizeof(*ebw));
        ebw->left = sl; ebw->top = st; ebw->width = sw; ebw->height = sh;
    }
    NewList(&ebw->chooserList);
    ebw->dc = dc; /* borrowed -- see fs3eemojibox.h */

    ebw->gridClass = MakeClass(NULL, "gadgetclass", NULL,
                               sizeof(FSEBGridInst), 0);
    if (!ebw->gridClass) return FALSE;
    ebw->gridClass->cl_Dispatcher.h_Entry = (HOOKFUNC)FSEBGrid_Dispatch;
    bdbprintf_makeclass("FSEBGridClass", ebw->gridClass);

    for (i = 0; i < FS3EEMOJIBOX_NUM_SETS; i++) {
        struct Node *node = NULL;
        if (ChooserBase)
            node = AllocChooserNode(CNA_Text, (ULONG)emojiSets[i].name, TAG_END);
        ebw->chooserNodes[i] = node;
        if (node) AddTail(&ebw->chooserList, node);
    }

    ebw->chooser = (Object *)NewObject(CHOOSER_GetClass(), NULL,
        GA_ID,             (ULONG)GID_EMOJIBOX_CHOOSER,
        GA_RelVerify,      TRUE,
        ICA_TARGET,        (ULONG)TargetInstance,
        CHOOSER_PopUp,     TRUE,
        CHOOSER_Labels,    (ULONG)&ebw->chooserList,
        CHOOSER_Active,    0UL,
        TAG_END);
    if (!ebw->chooser) return FALSE;

    setLabel = (Object *)NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)"Set:",
        TAG_END);

    topBar = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,  LAYOUT_ORIENT_HORIZ,
        LAYOUT_BottomSpacing, 4,
        LAYOUT_AddChild, (ULONG)NewObject(BUTTON_GetClass(), NULL,
                              GA_ReadOnly, TRUE,
                              BUTTON_BevelStyle, BVS_NONE,
                              BUTTON_Transparent, TRUE,
                              TAG_END),
            CHILD_WeightedWidth, 1,
        LAYOUT_AddChild, (ULONG)setLabel,
            CHILD_WeightedWidth, 0,
        LAYOUT_AddChild, (ULONG)ebw->chooser,
            CHILD_WeightedWidth, 0,
            CHILD_MinWidth, 180,
        TAG_END);
    if (!topBar) return FALSE;

    ebw->gridGadget = (Object *)NewObject(ebw->gridClass, NULL,
        GA_ID,         (ULONG)GID_EMOJIBOX_GRID,
        GA_RelVerify,  TRUE,
        ICA_TARGET,    (ULONG)TargetInstance,
        FSEB_EmojiSet, (ULONG)emojiSets[0].emojis,
        TAG_END);
    if (!ebw->gridGadget) return FALSE;

    ((FSEBGridInst *)INST_DATA(ebw->gridClass, ebw->gridGadget))->dc = ebw->dc;

    ebw->mainLayout = (Object *)NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_SpaceOuter,    FALSE,
        LAYOUT_InnerSpacing,  2,
        LAYOUT_LeftSpacing,   2,
        LAYOUT_RightSpacing,  2,
        LAYOUT_TopSpacing,    2,
        LAYOUT_BottomSpacing, 2,
        LAYOUT_AddChild, (ULONG)topBar,
            CHILD_WeightedHeight, 0,
        LAYOUT_AddChild, (ULONG)ebw->gridGadget,
            CHILD_WeightedHeight, 1,
        TAG_END);
    if (!ebw->mainLayout) return FALSE;

    ebw->windowObj = (Object *)NewObject(WINDOW_GetClass(), NULL,
        WA_Left,   120,
        WA_Top,    100,
        WA_Width,  FSEBW_OPEN_W,
        WA_Height, FSEBW_OPEN_H,
        WA_MinWidth,  FSEBW_MIN_W,
        WA_MinHeight, FSEBW_MIN_H,
        WA_IDCMP,  IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_NEWSIZE | IDCMP_RAWKEY,
        WA_Flags,  WFLG_DRAGBAR | WFLG_DEPTHGADGET | WFLG_CLOSEGADGET |
                   WFLG_SIZEGADGET | WFLG_ACTIVATE | WFLG_SMART_REFRESH,
        WA_Title,  (ULONG)"Emoji Box",
        WINDOW_ParentGroup, (ULONG)ebw->mainLayout,
        TAG_END);

    return (ebw->windowObj != NULL);
}

void FS3EEmojiBoxWindow_Dispose(FS3EEmojiBoxWindow *ebw)
{
    int i;
    if (!ebw) return;

    FS3EEmojiBoxWindow_Close(ebw);

    if (ebw->windowObj) {
        DisposeObject(ebw->windowObj);
        ebw->windowObj = NULL;
    }

    if (ebw->gridClass) {
        bdbprintf_freeclass("FSEBGridClass", ebw->gridClass);
        FreeClass(ebw->gridClass);
        ebw->gridClass = NULL;
    }

    /* ebw->dc is borrowed (see fs3eemojibox.h) -- not released here. */

    if (ChooserBase) {
        for (i = 0; i < FS3EEMOJIBOX_NUM_SETS; i++) {
            if (ebw->chooserNodes[i]) {
                FreeChooserNode(ebw->chooserNodes[i]);
                ebw->chooserNodes[i] = NULL;
            }
        }
    }
}

void FS3EEmojiBoxWindow_Open(FS3EEmojiBoxWindow *ebw)
{
    if (!ebw || !ebw->windowObj) return;

    if (ebw->window) {
        WindowToFront(ebw->window);
        ActivateWindow(ebw->window);
        return;
    }

    if (CurrentMainScreen)
        SetAttrs(ebw->windowObj, WA_CustomScreen, (ULONG)CurrentMainScreen, TAG_END);

    if (ebw->width > 0) {
        SetAttrs(ebw->windowObj,
                 WA_Left,   (ULONG)ebw->left,
                 WA_Top,    (ULONG)ebw->top,
                 WA_Width,  (ULONG)ebw->width,
                 WA_Height, (ULONG)ebw->height,
                 TAG_END);
    }

    ebw->window = (struct Window *)DoMethod(ebw->windowObj, WM_OPEN, NULL);
}

void FS3EEmojiBoxWindow_Close(FS3EEmojiBoxWindow *ebw)
{
    if (!ebw || !ebw->windowObj || !ebw->window) return;

    GetAttr(WA_Left,   ebw->windowObj, (ULONG *)&ebw->left);
    GetAttr(WA_Top,    ebw->windowObj, (ULONG *)&ebw->top);
    GetAttr(WA_Width,  ebw->windowObj, (ULONG *)&ebw->width);
    GetAttr(WA_Height, ebw->windowObj, (ULONG *)&ebw->height);

    DoMethod(ebw->windowObj, WM_CLOSE, NULL);
    ebw->window = NULL;
}

BOOL FS3EEmojiBoxWindow_HandleInput(FS3EEmojiBoxWindow *ebw)
{
    ULONG result;

    if (!ebw || !ebw->windowObj) return FALSE;
    if (!ebw->window) return TRUE;

    while ((result = DoMethod(ebw->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3EEmojiBoxWindow_Close(ebw);
                return TRUE;

            case WMHI_NEWSIZE:
                if (ebw->gridGadget)
                    RefreshGList((struct Gadget *)ebw->gridGadget, ebw->window, NULL, 1);
                break;

            // case WMHI_VANILLAKEY:
            // {
            //     ULONG key = result & 0x00FF;
            //     if (key == 0x1b) { /* Esc closes the popup */
            //         FS3EEmojiBoxWindow_Close(ebw);
            //         return TRUE;
            //     }
            //     break;
            // }

            case WMHI_RAWKEY:
            {
                ULONG key = result & 0x07f;
                ULONG isUp = (result & 0x080);
                ULONG qualifiers=0;

                GetAttr(WINDOW_Qualifier,ebw->windowObj,&qualifiers);
                if (key == 0x45 && isUp) { /* Esc */
                     FS3EEmojiBoxWindow_Close(ebw);
                    return TRUE;
                }

                if(!isUp && key>=0x50 && key<=0x59 && app->tootView.window)
                {
                    FS3EEmojiBox_HandleFKey(ebw,
                            app->tootView.bodyEditor,key, qualifiers, app->tootView.window);
                }

            }
                break;
            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;

                if (gadId == GID_EMOJIBOX_CHOOSER) {
                    /* Switch emoji set -- purely local to this window,
                     * handled inline like FS3ETootView_UpdateCharCount is. */
                    ULONG newIdx = 0;
                    if (ebw->chooser)
                        GetAttr(CHOOSER_Active, ebw->chooser, &newIdx);
                    if ((int)newIdx < FS3EEMOJIBOX_NUM_SETS &&
                        (int)newIdx != ebw->currentSetIdx)
                    {
                        ebw->currentSetIdx = (int)newIdx;
                        if (ebw->gridGadget)
                            SetGadgetAttrs((struct Gadget *)ebw->gridGadget,
                                ebw->window, NULL,
                                FSEB_EmojiSet, (ULONG)emojiSets[newIdx].emojis,
                                TAG_DONE);
                    }
                }

                /* GID_EMOJIBOX_GRID (a cell was clicked) is a cross-cutting
                 * concern -- this window doesn't know or care where the
                 * emoji should end up, so leave it for the central
                 * dispatch (friendsh3ep.c) via the BoopsiDelay queue,
                 * same as every other gadget in this app. */
                BoopsiDelay_BeginMessage(DelayQueue, gadId);
                BoopsiDelay_AddTag(DelayQueue, GA_Selected, 0);
                BoopsiDelay_EndMessage(DelayQueue);
                break;
            }

            default:
                break;
        }
    }

    return TRUE;
}

ULONG FS3EEmojiBoxWindow_GetSignalMask(FS3EEmojiBoxWindow *ebw)
{
    if (!ebw || !ebw->window) return 0;
    return (1L << ebw->window->UserPort->mp_SigBit);
}

void FS3EEmojiBoxWindow_GetWindowPos(FS3EEmojiBoxWindow *ebw)
{
    if (!ebw || !ebw->windowObj || !ebw->window) return;

    GetAttr(WA_Left,   ebw->windowObj, (ULONG *)&ebw->left);
    GetAttr(WA_Top,    ebw->windowObj, (ULONG *)&ebw->top);
    GetAttr(WA_Width,  ebw->windowObj, (ULONG *)&ebw->width);
    GetAttr(WA_Height, ebw->windowObj, (ULONG *)&ebw->height);
}

void FS3EEmojiBoxWindow_FlushPendingRender(FS3EEmojiBoxWindow *ebw)
{
    if (!s_pendingGridRender) return;
    s_pendingGridRender = FALSE;
    if (!ebw || !ebw->window || !ebw->gridGadget) return;
    RefreshGList((struct Gadget *)ebw->gridGadget, ebw->window, NULL, 1);
}

const char *FS3EEmojiBoxWindow_GetClickedUTF8(FS3EEmojiBoxWindow *ebw)
{
    ULONG cidx = (ULONG)(-1);
    int setIdx;

    if (!ebw || !ebw->gridGadget) return NULL;

    GetAttr(FSEB_ClickedIdx, ebw->gridGadget, &cidx);
    if (cidx >= 40) return NULL;

    setIdx = ebw->currentSetIdx;
    if (setIdx < 0 || setIdx >= FS3EEMOJIBOX_NUM_SETS) setIdx = 0;

    return emojiSets[setIdx].emojis[cidx];
}


BOOL FS3EEmojiBox_HandleFKey(FS3EEmojiBoxWindow *ebw, Object *editor, ULONG code, ULONG qualifiers,
                         struct Window *win)
{
    ULONG idx;
    int setIdx;
    const char *emoji;

    if (code < EMOJIBOX_RAWKEY_F1 || code > EMOJIBOX_RAWKEY_F10)
        return FALSE;
    if (!ebw || !editor)
        return FALSE;

    idx = code - EMOJIBOX_RAWKEY_F1;
    if (qualifiers & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT)) idx += 10;
    if (qualifiers & IEQUALIFIER_CONTROL)                        idx += 20;

    setIdx = ebw->currentSetIdx;
    if (setIdx < 0 || setIdx >= FS3EEMOJIBOX_NUM_SETS) setIdx = 0;
    emoji = emojiSets[setIdx].emojis[idx];
    if (!emoji) return FALSE;

    SetGadgetAttrs(editor, win, NULL,
                   UTED_InsertText, (ULONG)emoji,
                   TAG_END);
    return TRUE;
}

