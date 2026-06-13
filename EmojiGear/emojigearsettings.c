
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/graphics.h>

#include "emojigearsettings.h"
#include "tooltypepref.h"
#include "emojigear.h"
#include <stdio.h>
static char *StrDup(const char *s)
{
    char *copy;
    ULONG len;
    if(!s) return NULL;
    len = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if(copy) strcpy(copy, s);
    return copy;
}

/* Tooltype key names */
#define TT_RECENT          "RECENT"          /* RECENT0, RECENT1, ... RECENT7 */
#define TT_USE_WORKBENCH   "FSUSEWBMODEID"   /* "1" or "0" */
#define TT_SCREENMODEID    "SCREENMODEID"    /* 8 hex digits, e.g. "00029000" */
#define TT_EDITORBGCOLOR   "EDITORBGCOLOR"   /* 6 hex digits RRGGBB, e.g. "FFFFFF" */
#define TT_EDITORPENCOLOR  "EDITORPENCOLOR"  /* 6 hex digits RRGGBB, e.g. "000000" */
#define TT_TABSPACES       "TABSPACES"       /* decimal integer 2..12, default 4   */
#define TT_VISUALIZETABS   "VISUALIZETABS"   /* "1" or "0", default 0              */
#define TT_TABSARESPACES   "TABSARESPACES"   /* "1" or "0", default 0              */
#define TT_DISPLAYUNICODEINFO "DISPLAYUNICODEINFO" /* "1" or "0", default 0        */
#define TT_WORDWRAP        "WORDWRAP"        /* "1" or "0", default 1              */
#define TT_MONOSPACE       "FORCEMONOSPACE"  /* "1" or "0", default 0              */
#define TT_APPLYANSI       "APPLYANSI"       /* "1" or "0", default 0              */
#define TT_ANTIALIAS       "ANTIALIAS"       /* "1" or "0", default 0              */
#define TT_EMOJIQUALITY    "EMOJIQUALITY"    /* "1" or "0", default 0              */
#define TT_PRIMARYFONT     "PRIMARYFONT"     /* absolute path to .ttf/.otf file    */
#define TT_FALLBACK1FONT   "FALLBACK1FONT"   /* absolute path to fallback font 1   */
#define TT_FALLBACK2FONT   "FALLBACK2FONT"   /* absolute path to fallback font 2   */
#define TT_EMOJIFONT       "EMOJIFONT"       /* absolute path to emoji font        */
// #define TT_USEONECLORBG  "USEONECLORBG"  /* "1" or "0" */
// #define TT_BGIMAGE       "BGIMAGE"       /* absolute image file path */
// #define TT_PALETTE       "PALETTE"

void AppSettings_Load(AppSettings *as)
{
    int i;
    const char *val;
    char key[16]; /* "RECENT0" .. "RECENT7" */

    if(!as) return;

    /* ToolTypePrefs_Init is to be done at ebgining of main. */

    /* Load editor colors */
    {
        ULONG colorloaded = FALSE;
        as->editorBgColor  = 0x00AAAAAAUL; /* grey default, it's amiga */

        val = ToolTypePrefs_Get(TT_EDITORBGCOLOR);
        if (val && val[0] != '\0') {
            unsigned long parsed = 0;
            if(val[0] == '#')
            {
                int done = sscanf(val, "#%lX", &parsed);
                if(done)
                {
                    as->editorBgColor = (ULONG)parsed;
                    colorloaded = TRUE;
                }
            }
        }
        if(!colorloaded)
        {
            // it's important we get the current screen 0 color by default if no prefs
            struct Screen *locked = LockPubScreen(NULL);
            if(locked)
            {
                ULONG RGB32Colors[3];

                GetRGB32(locked->ViewPort.ColorMap,0,1,&RGB32Colors[0]);
                as->editorBgColor =
                    ((RGB32Colors[0]>>24)<<16)|
                    ((RGB32Colors[1]>>24)<<8)|
                    (RGB32Colors[2]>>24);

                UnlockPubScreen(NULL,locked);
            }
        }
    }

    as->editorPenColor = 0x00000000UL; /* black default */

    val = ToolTypePrefs_Get(TT_EDITORPENCOLOR);
    if (val && val[0] != '\0') {
        unsigned long parsed = 0;
        sscanf(val, "#%lX", &parsed);
        as->editorPenColor = (ULONG)parsed;
    }



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

 //   val = ToolTypePrefs_Get(TT_PALETTE);

    // as->currentPalette = PALETTE_PETMATE;
    // if (val && val[0] != '\0') {
    //     if(strcmp(val,c64PaletteNames[PALETTE_COLODORE])==0) as->currentPalette = PALETTE_COLODORE;
    //     else if(strcmp(val,c64PaletteNames[PALETTE_PEPTO])==0) as->currentPalette = PALETTE_PEPTO;
    //     else if(strcmp(val,c64PaletteNames[PALETTE_VICE])==0) as->currentPalette = PALETTE_VICE;
    // }

    /* Load UI background settings */
    // val = ToolTypePrefs_Get(TT_USEONECLORBG);
    // if(val) as->useOneColorBg = 1;
    // else  as->useOneColorBg = 0;

    // if(as->bgImagePath) FreeVec(as->bgImagePath);
    // as->bgImagePath = NULL;
    // val = ToolTypePrefs_Get(TT_BGIMAGE);
    // if (val && val[0] != '\0') {
    //     as->bgImagePath = StrDup(val);
    // }

    /* Load font size index */
    {
        extern const int fontSizeTable[];
        extern const int fontSizeTableCount;
        int tableCount = fontSizeTableCount;
        int best = 2; /* default: 16pt */
        val = ToolTypePrefs_Get("FONTSIZE");
        if (val && val[0] != '\0') {
            int size = atoi(val);
            int i, bestDiff = abs(fontSizeTable[0] - size);
            best = 0;
            for (i = 1; i < tableCount; i++) {
                int diff = abs(fontSizeTable[i] - size);
                if (diff < bestDiff) { bestDiff = diff; best = i; }
            }
        }
        as->currentFontSizeIndex = best;
    }

    /* Load font rendering settings */
    as->wordWrap = 0;  /* default off */
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

    as->primaryFontPath = NULL;
    val = ToolTypePrefs_Get(TT_PRIMARYFONT);
    if (val && val[0] != '\0') as->primaryFontPath = StrDup(val);
    else as->primaryFontPath = StrDup("LiberationSans-Regular.ttf");

    as->fallback1FontPath = NULL;
    val = ToolTypePrefs_Get(TT_FALLBACK1FONT);
    if (val && val[0] != '\0') as->fallback1FontPath = StrDup(val);

    as->fallback2FontPath = NULL;
    val = ToolTypePrefs_Get(TT_FALLBACK2FONT);
    if (val && val[0] != '\0') as->fallback2FontPath = StrDup(val);

    as->emojiFontPath = NULL;
    val = ToolTypePrefs_Get(TT_EMOJIFONT);
    if (val && val[0] != '\0') as->emojiFontPath = StrDup(val);
    else as->emojiFontPath = StrDup("OpenMoji-black-glyf.ttf");

    /* Load last directory */
    as->lastDir = NULL;
    val = ToolTypePrefs_Get("LASTDIR");
    if (val && val[0] != '\0')
        as->lastDir = StrDup(val);

    /* Load recent files */
    as->recentCount = 0;
    for(i = 0; i < APPSETTINGS_MAX_RECENT; i++) {
        sprintf(key, "%s%d", TT_RECENT, i);
        val = ToolTypePrefs_Get(key);
        if(val && val[0] != '\0') {
            BPTR lock = Lock((STRPTR)val, ACCESS_READ);
            if(lock) {
                char enckey[20];
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
    /* fullscreen and window position */
    {
        const char *p;
        // const char *p = ToolTypePrefs_Get("FULLSCREEN");
        // app->mainwindow.fullscreen = (p != NULL);
        p = ToolTypePrefs_Get("WINDOW");
        if(p)
        {
            app->mainwindow.left = 0;
            app->mainwindow.top = 0;
            app->mainwindow.width = 0;
            app->mainwindow.height = 0;

             sscanf(p,"%d:%d:%d:%d",&app->mainwindow.left,&app->mainwindow.top,
                            &app->mainwindow.width,&app->mainwindow.height);
        }

        p = ToolTypePrefs_Get("SEARCHWINDOW");
        if(p)
        {
            app->searchBox.left = 0;
            app->searchBox.top = 0;
            app->searchBox.width = 0;
            app->searchBox.height = 0;
            sscanf(p, "%d:%d:%d:%d", &app->searchBox.left, &app->searchBox.top,
                   &app->searchBox.width, &app->searchBox.height);
        }

        p = ToolTypePrefs_Get("EMOJIWINDOW");
        if(p)
        {
            app->emojiBoxWindow.left = 0;
            app->emojiBoxWindow.top = 0;
            app->emojiBoxWindow.width = 0;
            app->emojiBoxWindow.height = 0;
            sscanf(p, "%d:%d:%d:%d", &app->emojiBoxWindow.left, &app->emojiBoxWindow.top,
                   &app->emojiBoxWindow.width, &app->emojiBoxWindow.height);
        }
    }

}

void AppSettings_Save(AppSettings *as)
{
    int i;
    char key[32];

    if(!as) return;

    /* Save editor colors */
    {
        char hexbuf[8];
        sprintf(hexbuf, "#%06lX", (unsigned long)(as->editorBgColor & 0x00FFFFFFUL));
        ToolTypePrefs_Set(TT_EDITORBGCOLOR, hexbuf);
        sprintf(hexbuf, "#%06lX", (unsigned long)(as->editorPenColor & 0x00FFFFFFUL));
        ToolTypePrefs_Set(TT_EDITORPENCOLOR, hexbuf);
    }

    {
        char tsbuf[4];
        int v = as->tabSpaces;
        if (v < 2)  v = 2;
        if (v > 12) v = 12;
        sprintf(tsbuf, "%d", v);
        ToolTypePrefs_Set(TT_TABSPACES, tsbuf);
    }

    ToolTypePrefs_Set(TT_VISUALIZETABS, as->visualizeTabs ? "1" : "0");
    ToolTypePrefs_Set(TT_TABSARESPACES, as->tabsAreSpaces ? "1" : "0");
    ToolTypePrefs_Set(TT_DISPLAYUNICODEINFO, as->displayUnicodeInfo ? "1" : "0");

    /* Save font rendering settings */
    ToolTypePrefs_Set(TT_WORDWRAP,     as->wordWrap     ? "1" : "0");
    ToolTypePrefs_Set(TT_MONOSPACE,    as->monospace    ? "1" : "0");
    ToolTypePrefs_Set(TT_APPLYANSI,    as->applyAnsi    ? "1" : "0");
    ToolTypePrefs_Set(TT_ANTIALIAS,    as->antialias    ? "1" : "0");
    ToolTypePrefs_Set(TT_EMOJIQUALITY, as->emojiQuality ? "1" : "0");

    if (as->primaryFontPath && as->primaryFontPath[0] != '\0')
        ToolTypePrefs_Set(TT_PRIMARYFONT, as->primaryFontPath);
    else
        ToolTypePrefs_Remove(TT_PRIMARYFONT);

    if (as->fallback1FontPath && as->fallback1FontPath[0] != '\0')
        ToolTypePrefs_Set(TT_FALLBACK1FONT, as->fallback1FontPath);
    else
        ToolTypePrefs_Remove(TT_FALLBACK1FONT);

    if (as->fallback2FontPath && as->fallback2FontPath[0] != '\0')
        ToolTypePrefs_Set(TT_FALLBACK2FONT, as->fallback2FontPath);
    else
        ToolTypePrefs_Remove(TT_FALLBACK2FONT);

    if (as->emojiFontPath && as->emojiFontPath[0] != '\0')
        ToolTypePrefs_Set(TT_EMOJIFONT, as->emojiFontPath);
    else
        ToolTypePrefs_Remove(TT_EMOJIFONT);

    // if(as->currentPalette == PALETTE_PETMATE)
    // {
    //     ToolTypePrefs_Remove(TT_PALETTE);
    // } else if(as->currentPalette<PALETTE_COUNT) {
    //     ToolTypePrefs_Set( TT_PALETTE, c64PaletteNames[as->currentPalette] );
    // }

    /* Save UI background settings */
    // if( as->useOneColorBg)
    // {
    //     ToolTypePrefs_Set(TT_USEONECLORBG,NULL);
    // } else ToolTypePrefs_Remove(TT_USEONECLORBG);

    // if (as->bgImagePath && as->bgImagePath[0] != '\0') {
    //     ToolTypePrefs_Set(TT_BGIMAGE, as->bgImagePath);
    // } else {
    //     ToolTypePrefs_Remove(TT_BGIMAGE);
    // }

    // if(app->mainwindow.fullscreen)
    // {
    //     ToolTypePrefs_Set("FULLSCREEN",NULL);
    // }
    // else
    {
        char buf[32];
       // ToolTypePrefs_Remove("FULLSCREEN");
        BMainWindow_GetWindowPos(&app->mainwindow,app->window_obj);
        snprintf(buf,31,"%d:%d:%d:%d",app->mainwindow.left,app->mainwindow.top,
                           app->mainwindow.width,app->mainwindow.height );
         ToolTypePrefs_Set("WINDOW",buf);
    }

    /* Search box window position */
    {
        LONG sl = app->searchBox.left, st = app->searchBox.top;
        LONG sw = app->searchBox.width, sh = app->searchBox.height;
        if (app->searchBox.window) {
            GetAttr(WA_Left,   app->searchBox.windowObj, (ULONG *)&sl);
            GetAttr(WA_Top,    app->searchBox.windowObj, (ULONG *)&st);
            GetAttr(WA_Width,  app->searchBox.windowObj, (ULONG *)&sw);
            GetAttr(WA_Height, app->searchBox.windowObj, (ULONG *)&sh);
        }
        if (sw > 0) {
            char buf[32];
            snprintf(buf, 31, "%d:%d:%d:%d", (int)sl, (int)st, (int)sw, (int)sh);
            ToolTypePrefs_Set("SEARCHWINDOW", buf);
        } else {
            ToolTypePrefs_Remove("SEARCHWINDOW");
        }
    }

    /* Emoji box window position */
    {
        LONG el = app->emojiBoxWindow.left, et = app->emojiBoxWindow.top;
        LONG ew = app->emojiBoxWindow.width, eh = app->emojiBoxWindow.height;
        if (app->emojiBoxWindow.window) {
            GetAttr(WA_Left,   app->emojiBoxWindow.windowObj, (ULONG *)&el);
            GetAttr(WA_Top,    app->emojiBoxWindow.windowObj, (ULONG *)&et);
            GetAttr(WA_Width,  app->emojiBoxWindow.windowObj, (ULONG *)&ew);
            GetAttr(WA_Height, app->emojiBoxWindow.windowObj, (ULONG *)&eh);
        }
        if (ew > 0) {
            char buf[32];
            snprintf(buf, 31, "%d:%d:%d:%d", (int)el, (int)et, (int)ew, (int)eh);
            ToolTypePrefs_Set("EMOJIWINDOW", buf);
        } else {
            ToolTypePrefs_Remove("EMOJIWINDOW");
        }
    }

    /* Save font size as point size value */
    {
        extern const int fontSizeTable[];
        extern const int fontSizeTableCount;
        int tableCount = fontSizeTableCount;
        int idx = as->currentFontSizeIndex;
        char sizebuf[4];
        if (idx < 0) idx = 0;
        if (idx >= tableCount) idx = tableCount - 1;
        snprintf(sizebuf, sizeof(sizebuf), "%d", fontSizeTable[idx]);
        ToolTypePrefs_Set("FONTSIZE", sizebuf);
    }

    /* Save last directory */
    if (as->lastDir && as->lastDir[0] != '\0')
        ToolTypePrefs_Set("LASTDIR", as->lastDir);
    else
        ToolTypePrefs_Remove("LASTDIR");

    /* Save recent files */
    for(i = 0; i < APPSETTINGS_MAX_RECENT; i++) {
        char enckey[20];
        snprintf(key, 31, "%s%d", TT_RECENT, i);
        snprintf(enckey, 19, "RECENTENC%d", i);
        if(i < as->recentCount && as->recentFiles[i]) {
            char encbuf[4];
            ToolTypePrefs_Set(key, as->recentFiles[i]);
            snprintf(encbuf, 3, "%d", as->recentEncodings[i]);
            ToolTypePrefs_Set(enckey, encbuf);
        } else {
            ToolTypePrefs_Remove(key);
            ToolTypePrefs_Remove(enckey);
        }
    }

    ToolTypePrefs_Save();
}

void AppSettings_Close(AppSettings *as)
{
    int i;
    if(!as) return;

    FreeVec(as->lastDir);
    as->lastDir = NULL;

    FreeVec(as->primaryFontPath);  as->primaryFontPath  = NULL;
    FreeVec(as->fallback1FontPath); as->fallback1FontPath = NULL;
    FreeVec(as->fallback2FontPath); as->fallback2FontPath = NULL;
    FreeVec(as->emojiFontPath);    as->emojiFontPath    = NULL;

    for(i = 0; i < APPSETTINGS_MAX_RECENT; i++) {
        FreeVec(as->recentFiles[i]);
        as->recentFiles[i] = NULL;
    }
    as->recentCount = 0;

    ToolTypePrefs_Close();
}


void AppSettings_AddRecentFile(AppSettings *as, const char *path, int encoding)
{
    int i;
    int existingIndex = -1;

    if(!as || !path || path[0] == '\0') return;

    for(i = 0; i < as->recentCount; i++) {
        if(as->recentFiles[i] && strcmp(as->recentFiles[i], path) == 0) {
            existingIndex = i;
            break;
        }
    }

    if(existingIndex == 0) {
        /* Already at top: update encoding in case it changed */
        as->recentEncodings[0] = encoding;
        return;
    }

    if(existingIndex > 0) {
        /* Move to top, shifting others down, preserving encodings */
        char *existing    = as->recentFiles[existingIndex];
        for(i = existingIndex; i > 0; i--) {
            as->recentFiles[i]     = as->recentFiles[i - 1];
            as->recentEncodings[i] = as->recentEncodings[i - 1];
        }
        as->recentFiles[0]     = existing;
        as->recentEncodings[0] = encoding;
    } else {
        /* New entry: drop last if full, shift all down, insert at [0] */
        if(as->recentCount >= APPSETTINGS_MAX_RECENT) {
            FreeVec(as->recentFiles[APPSETTINGS_MAX_RECENT - 1]);
        }
        {
            int limit = as->recentCount < APPSETTINGS_MAX_RECENT ?
                        as->recentCount : APPSETTINGS_MAX_RECENT - 1;
            for(i = limit; i > 0; i--) {
                as->recentFiles[i]     = as->recentFiles[i - 1];
                as->recentEncodings[i] = as->recentEncodings[i - 1];
            }
        }
        as->recentFiles[0]     = StrDup(path);
        as->recentEncodings[0] = encoding;
        if(as->recentCount < APPSETTINGS_MAX_RECENT)
            as->recentCount++;
    }
}

int AppSettings_GetRecentCount(AppSettings *as)
{
    if(!as) return 0;
    return as->recentCount;
}

const char *AppSettings_GetRecentFile(AppSettings *as, int index)
{
    if(!as || index < 0 || index >= as->recentCount) return NULL;
    return as->recentFiles[index];
}

int AppSettings_GetRecentEncoding(AppSettings *as, int index)
{
    if(!as || index < 0 || index >= as->recentCount) return 0;
    return as->recentEncodings[index];
}

void AppSettings_SetLastDir(AppSettings *as, const char *dir)
{
    if (!as) return;
    FreeVec(as->lastDir);
    as->lastDir = (dir && dir[0] != '\0') ? StrDup(dir) : NULL;
}

