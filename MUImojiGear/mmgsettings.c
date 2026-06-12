/*
 * mmgsettings.c – Application settings for MUImojiGear.
 *
 * Adapted from EmojiGear/emojigearsettings.c:
 *   - Removed EmojiGear-specific window position handling (BMainWindow,
 *     searchBox, emojiBoxWindow); MUI persists window geometry via
 *     MUIA_Window_ID + MUIA_Application_Base automatically.
 *   - Removed dependency on emojigear.h and egaction.c for fontSizeTable.
 *   - Added AppSettings_ApplyToEditor() – equivalent to EmojiGear's
 *     UpdateEditorFontsFromSettings().
 */

#include "mmgsettings.h"
#include "../EmojiGear/tooltypepref.h"

#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <intuition/screens.h>
#include <gadgets/unitexteditor.h>
#include <proto/unitexteditor.h>
#include <gadgets/unibutton.h>
#include <proto/unibutton.h>
#include <libraries/utf8rastport.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mmgaction.h"

/* =========================================================================
 * Font size table (matches EmojiGear/egaction.c)
 * =========================================================================
 */
const int mmgFontSizeTable[]    = { 8, 12, 16, 20, 24, 28, 32, 36 };
const int mmgFontSizeTableCount = (int)(sizeof(mmgFontSizeTable) /
                                        sizeof(mmgFontSizeTable[0]));

/* =========================================================================
 * Internal helpers
 * =========================================================================
 */

static char *StrDup(const char *s)
{
    char *copy;
    ULONG len;
    if (!s) return NULL;
    len  = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) strcpy(copy, s);
    return copy;
}

/* Tooltype key names */
#define TT_EDITORBGCOLOR  "EDITORBGCOLOR"
#define TT_EDITORPENCOLOR "EDITORPENCOLOR"
#define TT_TABSPACES      "TABSPACES"
#define TT_VISUALIZETABS  "VISUALIZETABS"
#define TT_TABSARESPACES  "TABSARESPACES"
#define TT_DISPLAYUNICODEINFO "DISPLAYUNICODEINFO"
#define TT_WORDWRAP       "WORDWRAP"
#define TT_MONOSPACE      "FORCEMONOSPACE"
#define TT_APPLYANSI      "APPLYANSI"
#define TT_ANTIALIAS      "ANTIALIAS"
#define TT_EMOJIQUALITY   "EMOJIQUALITY"
#define TT_PRIMARYFONT    "PRIMARYFONT"
#define TT_FALLBACK1FONT  "FALLBACK1FONT"
#define TT_FALLBACK2FONT  "FALLBACK2FONT"
#define TT_EMOJIFONT      "EMOJIFONT"
#define TT_RECENT         "RECENT"

/* =========================================================================
 * AppSettings_Load
 * =========================================================================
 */
void AppSettings_Load(AppSettings *as)
{
    int i;
    const char *val;
    char key[24];
    BOOL colorBGLoaded = FALSE;
    BOOL colorPenLoaded = FALSE;
    if (!as) return;

    /* ---- Editor background colour ---- */
    {
        as->editorBgColor = 0x00AAAAAAUL; /* grey Amiga default */

        val = ToolTypePrefs_Get(TT_EDITORBGCOLOR);
        if (val && val[0] != '\0') {
            unsigned long parsed = 0;
            if (val[0] == '#' && sscanf(val, "#%lX", &parsed)) {
                as->editorBgColor = (ULONG)parsed;
                colorBGLoaded = TRUE;
            }
        }
        if (!colorBGLoaded) {
            /* Fall back to public screen colour 0 */
            struct Screen *scr = LockPubScreen(NULL);
            if (scr) {
                ULONG rgb[3];
                GetRGB32(scr->ViewPort.ColorMap, 0, 1, rgb);
                as->editorBgColor = ((rgb[0] >> 24) << 16)
                                  | ((rgb[1] >> 24) <<  8)
                                  |  (rgb[2] >> 24);
                UnlockPubScreen(NULL, scr);
            }
        }
    }

    /* ---- Editor pen colour ---- */
    as->editorPenColor = 0x00000000UL;
    val = ToolTypePrefs_Get(TT_EDITORPENCOLOR);
    if (val && val[0] != '\0') {
        unsigned long parsed = 0;
        sscanf(val, "#%lX", &parsed);
        as->editorPenColor = (ULONG)parsed;
        colorPenLoaded = TRUE;
    }
    as->colorsWereLoaded = (colorBGLoaded && colorPenLoaded);

    /* ---- Tab settings ---- */
    as->tabSpaces = 4;
    val = ToolTypePrefs_Get(TT_TABSPACES);
    if (val && val[0] != '\0') {
        int v = atoi(val);
        if (v < 2)  v = 2;
        if (v > 12) v = 12;
        as->tabSpaces = v;
    }

    as->visualizeTabs = FALSE;
    val = ToolTypePrefs_Get(TT_VISUALIZETABS);
    if (val && val[0] == '1') as->visualizeTabs = TRUE;

    as->tabsAreSpaces = FALSE;
    val = ToolTypePrefs_Get(TT_TABSARESPACES);
    if (val && val[0] == '1') as->tabsAreSpaces = TRUE;

    as->displayUnicodeInfo = FALSE;
    val = ToolTypePrefs_Get(TT_DISPLAYUNICODEINFO);
    if (val && val[0] == '1') as->displayUnicodeInfo = TRUE;

    /* ---- Font size index ---- */
    {
        int best = 2; /* default index → 16 pt */
        val = ToolTypePrefs_Get("FONTSIZE");
        if (val && val[0] != '\0') {
            int size    = atoi(val);
            int bestDiff = abs(mmgFontSizeTable[0] - size);
            int j;
            best = 0;
            for (j = 1; j < mmgFontSizeTableCount; j++) {
                int diff = abs(mmgFontSizeTable[j] - size);
                if (diff < bestDiff) { bestDiff = diff; best = j; }
            }
        }
        as->currentFontSizeIndex = best;
    }

    /* ---- Font rendering flags ---- */
    as->wordWrap  = 0;
    val = ToolTypePrefs_Get(TT_WORDWRAP);
    if (val) as->wordWrap = (val[0] == '0') ? 0 : 1;

    as->monospace = 0;
    val = ToolTypePrefs_Get(TT_MONOSPACE);
    if (val && val[0] == '1') as->monospace = 1;

    as->applyAnsi = 0;
    val = ToolTypePrefs_Get(TT_APPLYANSI);
    if (val && val[0] == '1') as->applyAnsi = 1;

    as->antialias = FALSE;
    val = ToolTypePrefs_Get(TT_ANTIALIAS);
    if (val && val[0] == '1') as->antialias = TRUE;

    as->emojiQuality = FALSE;
    val = ToolTypePrefs_Get(TT_EMOJIQUALITY);
    if (val && val[0] == '1') as->emojiQuality = TRUE;

    /* ---- Font paths ---- */
    as->primaryFontPath = NULL;
    val = ToolTypePrefs_Get(TT_PRIMARYFONT);
    as->primaryFontPath = (val && val[0] != '\0')
                        ? StrDup(val)
                        : StrDup("LiberationSans-Regular.ttf");

    as->fallback1FontPath = NULL;
    val = ToolTypePrefs_Get(TT_FALLBACK1FONT);
    if (val && val[0] != '\0') as->fallback1FontPath = StrDup(val);

    as->fallback2FontPath = NULL;
    val = ToolTypePrefs_Get(TT_FALLBACK2FONT);
    if (val && val[0] != '\0') as->fallback2FontPath = StrDup(val);

    as->emojiFontPath = NULL;
    val = ToolTypePrefs_Get(TT_EMOJIFONT);
    as->emojiFontPath = (val && val[0] != '\0')
                      ? StrDup(val)
                      : StrDup("NotoColorEmoji32.ttf");

    /* ---- Last directory ---- */
    as->lastDir = NULL;
    val = ToolTypePrefs_Get("LASTDIR");
    if (val && val[0] != '\0') as->lastDir = StrDup(val);

    /* ---- Recent files ---- */
    as->recentCount = 0;
    for (i = 0; i < APPSETTINGS_MAX_RECENT; i++) {
        char enckey[20];
        sprintf(key, "%s%d", TT_RECENT, i);
        val = ToolTypePrefs_Get(key);
        if (val && val[0] != '\0') {
            BPTR lock = Lock((STRPTR)val, ACCESS_READ);
            if (lock) {
                const char *encval;
                UnLock(lock);
                as->recentFiles[as->recentCount] = StrDup(val);
                sprintf(enckey, "RECENTENC%d", i);
                encval = ToolTypePrefs_Get(enckey);
                as->recentEncodings[as->recentCount] =
                    (encval && encval[0] != '\0') ? atoi(encval) : 0;
                as->recentCount++;
            }
        }
    }
}

/* =========================================================================
 * AppSettings_Save
 * MUI persists window geometry automatically via MUIA_Window_ID +
 * MUIA_Application_Base, so we only save editor/font/rendering prefs here.
 * =========================================================================
 */
void AppSettings_Save(AppSettings *as)
{
    int  i;
    char key[32];

    if (!as) return;

    /* ---- Editor colours ---- */
    {
        char buf[8];
        sprintf(buf, "#%06lX", (unsigned long)(as->editorBgColor  & 0x00FFFFFFUL));
        ToolTypePrefs_Set(TT_EDITORBGCOLOR,  buf);
        sprintf(buf, "#%06lX", (unsigned long)(as->editorPenColor & 0x00FFFFFFUL));
        ToolTypePrefs_Set(TT_EDITORPENCOLOR, buf);
    }

    /* ---- Tab settings ---- */
    {
        char buf[4];
        int v = as->tabSpaces;
        if (v < 2) v = 2; if (v > 12) v = 12;
        sprintf(buf, "%d", v);
        ToolTypePrefs_Set(TT_TABSPACES, buf);
    }
    ToolTypePrefs_Set(TT_VISUALIZETABS, as->visualizeTabs ? "1" : "0");
    ToolTypePrefs_Set(TT_TABSARESPACES, as->tabsAreSpaces ? "1" : "0");
    ToolTypePrefs_Set(TT_DISPLAYUNICODEINFO, as->displayUnicodeInfo ? "1" : "0");

    /* ---- Font rendering flags ---- */
    ToolTypePrefs_Set(TT_WORDWRAP,     as->wordWrap     ? "1" : "0");
    ToolTypePrefs_Set(TT_MONOSPACE,    as->monospace    ? "1" : "0");
    ToolTypePrefs_Set(TT_APPLYANSI,    as->applyAnsi    ? "1" : "0");
    ToolTypePrefs_Set(TT_ANTIALIAS,    as->antialias    ? "1" : "0");
    ToolTypePrefs_Set(TT_EMOJIQUALITY, as->emojiQuality ? "1" : "0");

    /* ---- Font size as point value ---- */
    {
        char buf[4];
        int idx = as->currentFontSizeIndex;
        if (idx < 0) idx = 0;
        if (idx >= mmgFontSizeTableCount) idx = mmgFontSizeTableCount - 1;
        sprintf(buf, "%d", mmgFontSizeTable[idx]);
        ToolTypePrefs_Set("FONTSIZE", buf);
    }

    /* ---- Font paths ---- */
    if (as->primaryFontPath  && as->primaryFontPath[0])
        ToolTypePrefs_Set(TT_PRIMARYFONT,  as->primaryFontPath);
    else
        ToolTypePrefs_Remove(TT_PRIMARYFONT);

    if (as->fallback1FontPath && as->fallback1FontPath[0])
        ToolTypePrefs_Set(TT_FALLBACK1FONT, as->fallback1FontPath);
    else
        ToolTypePrefs_Remove(TT_FALLBACK1FONT);

    if (as->fallback2FontPath && as->fallback2FontPath[0])
        ToolTypePrefs_Set(TT_FALLBACK2FONT, as->fallback2FontPath);
    else
        ToolTypePrefs_Remove(TT_FALLBACK2FONT);

    if (as->emojiFontPath && as->emojiFontPath[0])
        ToolTypePrefs_Set(TT_EMOJIFONT, as->emojiFontPath);
    else
        ToolTypePrefs_Remove(TT_EMOJIFONT);

    /* ---- Last directory ---- */
    if (as->lastDir && as->lastDir[0])
        ToolTypePrefs_Set("LASTDIR", as->lastDir);
    else
        ToolTypePrefs_Remove("LASTDIR");

    /* ---- Recent files ---- */
    for (i = 0; i < APPSETTINGS_MAX_RECENT; i++) {
        char enckey[20];
        sprintf(key,    "%s%d",       TT_RECENT, i);
        sprintf(enckey, "RECENTENC%d", i);
        if (i < as->recentCount && as->recentFiles[i]) {
            char encbuf[4];
            ToolTypePrefs_Set(key, as->recentFiles[i]);
            sprintf(encbuf, "%d", as->recentEncodings[i]);
            ToolTypePrefs_Set(enckey, encbuf);
        } else {
            ToolTypePrefs_Remove(key);
            ToolTypePrefs_Remove(enckey);
        }
    }

    ToolTypePrefs_Save();
}

/* =========================================================================
 * AppSettings_Close
 * =========================================================================
 */
void AppSettings_Close(AppSettings *as)
{
    int i;
    if (!as) return;

    FreeVec(as->lastDir);         as->lastDir         = NULL;
    FreeVec(as->primaryFontPath); as->primaryFontPath = NULL;
    FreeVec(as->fallback1FontPath); as->fallback1FontPath = NULL;
    FreeVec(as->fallback2FontPath); as->fallback2FontPath = NULL;
    FreeVec(as->emojiFontPath);   as->emojiFontPath   = NULL;

    for (i = 0; i < APPSETTINGS_MAX_RECENT; i++) {
        FreeVec(as->recentFiles[i]);
        as->recentFiles[i] = NULL;
    }
    as->recentCount = 0;

    ToolTypePrefs_Close();
}

/* =========================================================================
 * AppSettings_ApplyToEditor
 * Equivalent of EmojiGear's UpdateEditorFontsFromSettings().
 * Uses SetAttrs() on the MUI Boopsi object (Boopsi_Smart proxies it to
 * the underlying gadget) plus RefreshGList when the window is already open.
 * =========================================================================
 */
void AppSettings_ApplyToEditor(AppSettings *as, Object *editorMuiObj,
                               struct Gadget *rawG, struct Window *rawW)
{
    ULONG flags;
    int   idx;

    if (!as || !editorMuiObj) return;

    flags = (as->antialias    ? URP_PREF_ANTIALIAS        : 0)
          | (as->monospace    ? URP_PREF_FORCE_MONOSPACE   : 0)
          | (as->emojiQuality ? URP_PREF_HIGHFILTERING     : 0)
          | URP_PREF_CLUTMODE_NOMASK;

    idx = as->currentFontSizeIndex;
    if (idx < 0) idx = 0;
    if (idx >= mmgFontSizeTableCount) idx = mmgFontSizeTableCount - 1;

// printf("colorsWereLoaded %d\n",as->colorsWereLoaded);
 /*need to be done after open
    if(as->colorsWereLoaded)
    {
        MmgAction_ApplyColorFromSettings();
    }*/
    SetAttrs(editorMuiObj,
        UTED_FlushFonts,        TRUE,
        UTED_URPPrefs,          flags,
        UTED_WordWrap,          (ULONG)as->wordWrap,
        UTED_ApplyAnsiEscapes,  (ULONG)as->applyAnsi,
        UTED_TabSpaces,         (ULONG)as->tabSpaces,
        UTED_VisibleTabs,       (ULONG)as->visualizeTabs,
        UTED_TabsAreSpaces,     (ULONG)as->tabsAreSpaces,
        UTED_PointSize,         (ULONG)mmgFontSizeTable[idx],
        UTED_AddFont,           (ULONG)as->primaryFontPath,
        UTED_AddFont,           (ULONG)as->fallback1FontPath,
        UTED_AddFont,           (ULONG)as->fallback2FontPath,
        UTED_AddFont,           (ULONG)as->emojiFontPath,
        TAG_DONE);



    if (rawG && rawW)
        RefreshGList(rawG, rawW, NULL, 1);
}

/* =========================================================================
 * AppSettings_ApplyToEmojiButton
 *
 * Flush and reload fonts on the UniButton draw context (shared with the
 * EmojiBox grid DC).  Point size is fixed at 16 regardless of the editor
 * font size — the button is always a compact fixed-size widget.
 * Monospace flag is intentionally omitted: irrelevant for emoji rendering.
 * =========================================================================
 */
void AppSettings_ApplyToEmojiButton(AppSettings *as, Object *btnMuiObj)
{
    ULONG flags;
    if (!as || !btnMuiObj) return;

    flags = (as->antialias    ? URP_PREF_ANTIALIAS    : 0)
          | (as->emojiQuality ? URP_PREF_HIGHFILTERING : 0)
          | URP_PREF_CLUTMODE_NOMASK;

    SetAttrs(btnMuiObj,
        UBT_FlushFonts, TRUE,
        UBT_URPPrefs,   flags,
        UBT_PointSize,  16UL,
        UBT_AddFont,    (ULONG)as->primaryFontPath,
        UBT_AddFont,    (ULONG)as->fallback1FontPath,
        UBT_AddFont,    (ULONG)as->fallback2FontPath,
        UBT_AddFont,    (ULONG)as->emojiFontPath,
        TAG_DONE);
}

/* =========================================================================
 * Recent file management (self-contained, no EmojiGear dependencies)
 * =========================================================================
 */
void AppSettings_AddRecentFile(AppSettings *as, const char *path, int encoding)
{
    int i, existingIndex = -1;
    if (!as || !path || path[0] == '\0') return;

    for (i = 0; i < as->recentCount; i++) {
        if (as->recentFiles[i] && strcmp(as->recentFiles[i], path) == 0)
            { existingIndex = i; break; }
    }

    if (existingIndex == 0) {
        as->recentEncodings[0] = encoding;
        return;
    }

    if (existingIndex > 0) {
        char *existing = as->recentFiles[existingIndex];
        for (i = existingIndex; i > 0; i--) {
            as->recentFiles[i]     = as->recentFiles[i - 1];
            as->recentEncodings[i] = as->recentEncodings[i - 1];
        }
        as->recentFiles[0]     = existing;
        as->recentEncodings[0] = encoding;
    } else {
        int limit;
        if (as->recentCount >= APPSETTINGS_MAX_RECENT)
            FreeVec(as->recentFiles[APPSETTINGS_MAX_RECENT - 1]);
        limit = as->recentCount < APPSETTINGS_MAX_RECENT
                ? as->recentCount : APPSETTINGS_MAX_RECENT - 1;
        for (i = limit; i > 0; i--) {
            as->recentFiles[i]     = as->recentFiles[i - 1];
            as->recentEncodings[i] = as->recentEncodings[i - 1];
        }
        as->recentFiles[0]     = StrDup(path);
        as->recentEncodings[0] = encoding;
        if (as->recentCount < APPSETTINGS_MAX_RECENT) as->recentCount++;
    }
}

int AppSettings_GetRecentCount(AppSettings *as)
{
    return as ? as->recentCount : 0;
}

const char *AppSettings_GetRecentFile(AppSettings *as, int index)
{
    if (!as || index < 0 || index >= as->recentCount) return NULL;
    return as->recentFiles[index];
}

int AppSettings_GetRecentEncoding(AppSettings *as, int index)
{
    if (!as || index < 0 || index >= as->recentCount) return 0;
    return as->recentEncodings[index];
}

void AppSettings_SetLastDir(AppSettings *as, const char *dir)
{
    if (!as) return;
    FreeVec(as->lastDir);
    as->lastDir = (dir && dir[0] != '\0') ? StrDup(dir) : NULL;
}
