/*
 * fs3esettings.c - Application settings for FriendSh3ep.
 *
 * Pattern adapted from EmojiGear/emojigearsettings.c.
 * Uses tooltypepref API (EmojiGear/tooltypepref.c) for icon tooltype I/O.
 */

#include "fs3esettings.h"
#include "friendsh3ep.h"
#include "fs3eboopsimainwindow.h"
#include "../EmojiGear/tooltypepref.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/dos.h>

/* --------------------------------------------------------------------------
 * Tooltype key names
 * -------------------------------------------------------------------------- */
#define TT_PRIMARYFONT    "PRIMARYFONT"    /* path to primary .ttf/.otf          */
#define TT_FALLBACK1FONT  "FALLBACK1FONT"  /* path to fallback font 1            */
#define TT_FALLBACK2FONT  "FALLBACK2FONT"  /* path to fallback font 2            */
#define TT_EMOJIFONT      "EMOJIFONT"      /* path to UI emoji font (2-color glyph, navbar/buttons) */
#define TT_COLOREMOJIFONT "COLOREMOJIFONT" /* path to color emoji font (timeline post rendering)    */
#define TT_FONTSIZE       "FONTSIZE"       /* point size as decimal, e.g. "12"   */
#define TT_ANTIALIAS      "ANTIALIAS"      /* "1" or "0"                         */
#define TT_EMOJIQUALITY   "EMOJIQUALITY"   /* "1" or "0"                         */
#define TT_THEME          "THEME"          /* theme name string                   */
#define TT_WINDOW         "WINDOW"         /* "left:top:width:height"             */
#define TT_WINDOWALT      "WINDOWALT"      /* "left:top:width:height" ZipWindow alternate */
#define TT_CACHEPATH      "CACHEPATH"      /* media cache directory path          */

/* -------------------------------------------------------------------------- */

static char *StrDup(const char *s)
{
    char *copy;
    ULONG len;
    if (!s) return NULL;
    len = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) strcpy(copy, s);
    return copy;
}

/* -------------------------------------------------------------------------- */

void FS3ESettings_Load(FS3ESettings *s)
{
    const char *val;

    if (!s) return;

    ToolTypePrefs_Init("FriendSh3ep");

    /* Font paths */
    s->primaryFontPath = NULL;
    val = ToolTypePrefs_Get(TT_PRIMARYFONT);
    if (val && val[0] != '\0')
        s->primaryFontPath = StrDup(val);
    else
        s->primaryFontPath = StrDup("LiberationSans-Regular.ttf");

    s->fallback1FontPath = NULL;
    val = ToolTypePrefs_Get(TT_FALLBACK1FONT);
    if (val && val[0] != '\0')
        s->fallback1FontPath = StrDup(val);

    s->fallback2FontPath = NULL;
    val = ToolTypePrefs_Get(TT_FALLBACK2FONT);
    if (val && val[0] != '\0')
        s->fallback2FontPath = StrDup(val);

    s->emojiFontPath = NULL;
    val = ToolTypePrefs_Get(TT_EMOJIFONT);
    if (val && val[0] != '\0')
        s->emojiFontPath = StrDup(val);
    else
        s->emojiFontPath = StrDup("OpenMoji-black-glyf.ttf");

    s->colorEmojiFontPath = NULL;
    val = ToolTypePrefs_Get(TT_COLOREMOJIFONT);
    if (val && val[0] != '\0')
        s->colorEmojiFontPath = StrDup(val);
    else
        s->colorEmojiFontPath = StrDup("NotoColorEmoji32.ttf");

    /* Font point size */
    s->fontPointSize = 12;
    val = ToolTypePrefs_Get(TT_FONTSIZE);
    if (val && val[0] != '\0') {
        int sz = atoi(val);
        if (sz >= 6 && sz <= 72) s->fontPointSize = sz;
    }

    /* Font rendering flags */
    s->antialias = TRUE;
    val = ToolTypePrefs_Get(TT_ANTIALIAS);
    if (val) s->antialias = (val[0] != '0');

    s->emojiQuality = TRUE;
    val = ToolTypePrefs_Get(TT_EMOJIQUALITY);
    if (val) s->emojiQuality = (val[0] != '0');

    /* Theme */
    s->themeName = NULL;
    val = ToolTypePrefs_Get(TT_THEME);
    if (val && val[0] != '\0')
        s->themeName = StrDup(val);

    /* Media cache path */
    s->cachePath = NULL;
    val = ToolTypePrefs_Get(TT_CACHEPATH);
    if (val && val[0] != '\0')
        s->cachePath = StrDup(val);
    /* NULL cachePath → network process uses its own FS3ECACHE_DEFAULT_DIR */

    /* Main window position - written directly into app->mainwindow */
    if (app) {
        val = ToolTypePrefs_Get(TT_WINDOW);
        if (val && val[0] != '\0') {
            int l = 0, t = 0, w = 0, h = 0;
            sscanf(val, "%d:%d:%d:%d", &l, &t, &w, &h);
            if (w > 0 && h > 0) {
                app->mainwindow.left   = (LONG)l;
                app->mainwindow.top    = (LONG)t;
                app->mainwindow.width  = (LONG)w;
                app->mainwindow.height = (LONG)h;
            }
        }
    }

    /* Alternate window position for the altpos button */
    if (app) {
        val = ToolTypePrefs_Get(TT_WINDOWALT);
        if (val && val[0] != '\0') {
            int l = 0, t = 0, w = 0, h = 0;
            sscanf(val, "%d:%d:%d:%d", &l, &t, &w, &h);
            if (w > 0 && h > 0) {
                app->altWinLeft   = (LONG)l;
                app->altWinTop    = (LONG)t;
                app->altWinWidth  = (LONG)w;
                app->altWinHeight = (LONG)h;
            }
        }
    }
}

void FS3ESettings_Save(FS3ESettings *s)
{
    char buf[64];

    if (!s) return;

    /* Font paths */
    if (s->primaryFontPath && s->primaryFontPath[0] != '\0')
        ToolTypePrefs_Set(TT_PRIMARYFONT, s->primaryFontPath);
    else
        ToolTypePrefs_Remove(TT_PRIMARYFONT);

    if (s->fallback1FontPath && s->fallback1FontPath[0] != '\0')
        ToolTypePrefs_Set(TT_FALLBACK1FONT, s->fallback1FontPath);
    else
        ToolTypePrefs_Remove(TT_FALLBACK1FONT);

    if (s->fallback2FontPath && s->fallback2FontPath[0] != '\0')
        ToolTypePrefs_Set(TT_FALLBACK2FONT, s->fallback2FontPath);
    else
        ToolTypePrefs_Remove(TT_FALLBACK2FONT);

    if (s->emojiFontPath && s->emojiFontPath[0] != '\0')
        ToolTypePrefs_Set(TT_EMOJIFONT, s->emojiFontPath);
    else
        ToolTypePrefs_Remove(TT_EMOJIFONT);

    if (s->colorEmojiFontPath && s->colorEmojiFontPath[0] != '\0')
        ToolTypePrefs_Set(TT_COLOREMOJIFONT, s->colorEmojiFontPath);
    else
        ToolTypePrefs_Remove(TT_COLOREMOJIFONT);

    /* Font rendering */
    snprintf(buf, sizeof(buf), "%d", s->fontPointSize);
    ToolTypePrefs_Set(TT_FONTSIZE, buf);

    ToolTypePrefs_Set(TT_ANTIALIAS,    s->antialias    ? "1" : "0");
    ToolTypePrefs_Set(TT_EMOJIQUALITY, s->emojiQuality ? "1" : "0");

    /* Theme */
    if (s->themeName && s->themeName[0] != '\0')
        ToolTypePrefs_Set(TT_THEME, s->themeName);
    else
        ToolTypePrefs_Remove(TT_THEME);

    /* Media cache path — only write when non-default to keep the .info clean */
    if (s->cachePath && s->cachePath[0] != '\0')
        ToolTypePrefs_Set(TT_CACHEPATH, s->cachePath);
    else
        ToolTypePrefs_Remove(TT_CACHEPATH);

    /* Main window position */
    if (app && app->window_obj) {
        FS3EMain_GetWindowPos(&app->mainwindow, app->window_obj);
        if (app->mainwindow.width > 0 && app->mainwindow.height > 0) {
            snprintf(buf, sizeof(buf), "%d:%d:%d:%d",
                     (int)app->mainwindow.left,  (int)app->mainwindow.top,
                     (int)app->mainwindow.width, (int)app->mainwindow.height);
            ToolTypePrefs_Set(TT_WINDOW, buf);
        }
    }

    /* Alternate window position for the altpos button */
    if (app && app->altWinWidth > 0 && app->altWinHeight > 0) {
        snprintf(buf, sizeof(buf), "%d:%d:%d:%d",
                 (int)app->altWinLeft,  (int)app->altWinTop,
                 (int)app->altWinWidth, (int)app->altWinHeight);
        ToolTypePrefs_Set(TT_WINDOWALT, buf);
    } else {
        ToolTypePrefs_Remove(TT_WINDOWALT);
    }

    ToolTypePrefs_Save();
}

void FS3ESettings_Close(FS3ESettings *s)
{
    if (!s) return;

    FreeVec(s->primaryFontPath);   s->primaryFontPath   = NULL;
    FreeVec(s->fallback1FontPath); s->fallback1FontPath = NULL;
    FreeVec(s->fallback2FontPath); s->fallback2FontPath = NULL;
    FreeVec(s->emojiFontPath);      s->emojiFontPath      = NULL;
    FreeVec(s->colorEmojiFontPath); s->colorEmojiFontPath = NULL;
    FreeVec(s->themeName);          s->themeName          = NULL;
    FreeVec(s->cachePath);         s->cachePath         = NULL;

    ToolTypePrefs_Close();
}
