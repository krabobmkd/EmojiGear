/*
 * fs3ethemeview.c - Font & Theme Settings window for FriendSh3ep.
 *
 * See fs3ethemeview.h. Pattern adapted from EmojiGear/egfontsview.c.
 * Manages font paths/rendering options (like EgFontsView) and adds a
 * theme chooser group for selecting the UI theme by name.
 */

#include <stdio.h>
#include <string.h>

#include <clib/alib_protos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>
#include <proto/utility.h>

#include <proto/window.h>
#include <classes/window.h>

#include <proto/layout.h>
#include <gadgets/layout.h>

#include <proto/button.h>
#include <gadgets/button.h>

#include <proto/checkbox.h>
#include <gadgets/checkbox.h>

#include <proto/label.h>
#include <images/label.h>

#include <proto/getfile.h>
#include <gadgets/getfile.h>

#include <proto/chooser.h>
#include <gadgets/chooser.h>

#include <dos/dos.h>
#include <utility/hooks.h>
#include <libraries/asl.h>

#include "friendsh3ep.h"
#include "fs3ethemeview.h"
#include "fs3esettings.h"
#include "fs3elocale.h"
#include "fs3egadgetid.h"
#include "fs3eboopsimainwindow.h"

extern struct Library *CheckboxBase;
extern struct Library *GetFileBase;
extern struct Library *ChooserBase;

/* Updates buttonDC fonts and preference flags from app->settings. */
extern void FS3EApp_ApplyFontSettings(void);

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static char *ThemeStrDup(const char *s)
{
    ULONG len;
    char *copy;
    if (!s) return NULL;
    len  = (ULONG)strlen(s) + 1;
    copy = (char *)AllocVec(len, MEMF_ANY);
    if (copy) strcpy(copy, s);
    return copy;
}

/* ------------------------------------------------------------------ */
/* Theme scanning                                                      */
/* ------------------------------------------------------------------ */

/* Directory root scanned by FS3EThemeView_ScanThemes() -- see fs3estyle.h's
 * FS3ESTYLE_THEMES_ROOT, shared with FS3EApp_LoadTheme in friendsh3ep.c.
 * "PROGDIR:themes/mouton" (bundled with the app) is one such subdirectory,
 * but is not auto-loaded -- see FS3EStyle_SetThemePath's doc comment. */

/* Detaches CHOOSER_Labels (if attached), frees every node in
 * tv->themeList/themeNodes[]/themeNames[], and resets the list to empty.
 * Detaching first, same "Labels, ~0 then rebuild" idiom
 * FS3ELoginView_SetAccountsList uses for its ListBrowser (see
 * fs3eloginview.c), keeps the gadget from ever holding a pointer into a
 * node this is about to free. */
static void clearThemeNodes(FS3EThemeView *tv)
{
    int i;

    if (!ChooserBase) return;

    if (tv->window) {
        SetGadgetAttrs((struct Gadget *)tv->themeChooser, tv->window, NULL,
            CHOOSER_Labels, (ULONG)~0, TAG_DONE);
    } else if (tv->themeChooser) {
        SetAttrs(tv->themeChooser, CHOOSER_Labels, (ULONG)~0, TAG_END);
    }

    for (i = 0; i < tv->themeCount; i++) {
        if (tv->themeNodes[i]) FreeChooserNode(tv->themeNodes[i]);
        tv->themeNodes[i] = NULL;
        FreeVec(tv->themeNames[i]);
        tv->themeNames[i] = NULL;
    }
    NewList(&tv->themeList);
    tv->themeCount = 0;
}

/* Appends the always-present "-" (no theme) entry at index 0. Uses the
 * LOC()'d string directly, same as before this was pulled into its own
 * function -- a static-lifetime string needs no owned copy. */
static void addNoThemeNode(FS3EThemeView *tv)
{
    struct Node *node;

    if (!ChooserBase || tv->themeCount >= FS3ETHEMEVIEW_MAX_THEMES) return;

    node = AllocChooserNode(CNA_Text, (ULONG)LOC(MSG_THEMEV_THEME_NONE), TAG_END);
    if (!node) return;

    tv->themeNodes[tv->themeCount] = node;
    tv->themeNames[tv->themeCount] = NULL;
    tv->themeCount++;
    AddTail(&tv->themeList, node);
}

/* Appends one scanned-theme entry, displayed as dirName verbatim (no
 * prettification -- the chooser label IS the subdirectory name). Keeps
 * its own AllocVec'd copy of dirName alive in themeNames[] for as long as
 * the node exists and hands THAT pointer to CNA_Text, rather than relying
 * on the chooser to copy it itself -- chooser.gadget's CNA_CopyText is
 * OS4-only (see <gadgets/chooser.h>), so on OS3 there's no guarantee
 * CNA_Text doesn't just store the pointer it's given. */
static void addScannedThemeNode(FS3EThemeView *tv, const char *dirName)
{
    struct Node *node;
    char *ownedName;

    if (!ChooserBase || tv->themeCount >= FS3ETHEMEVIEW_MAX_THEMES) return;

    ownedName = ThemeStrDup(dirName);
    if (!ownedName) return;

    node = AllocChooserNode(CNA_Text, (ULONG)ownedName, TAG_END);
    if (!node) { FreeVec(ownedName); return; }

    tv->themeNodes[tv->themeCount] = node;
    tv->themeNames[tv->themeCount] = ownedName;
    tv->themeCount++;
    AddTail(&tv->themeList, node);
}

/* TRUE if PROGDIR:themes/<dirName>/style.txt exists -- a plain Open()
 * probe, same idiom FS3ECache_Lookup uses in
 * network_fs3e/fs3enet_cache.c. */
static BOOL hasStyleFile(const char *dirName)
{
    char path[300];
    BPTR fh;

    snprintf(path, sizeof(path), "%s/%s/style.txt", FS3ESTYLE_THEMES_ROOT, dirName);
    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) return FALSE;
    Close(fh);
    return TRUE;
}

/* Rebuilds the theme chooser's list from scratch: entry 0 is always "-"
 * (no theme), followed by every immediate subdirectory of
 * PROGDIR:themes/ that contains a style.txt (see hasStyleFile), in
 * whatever order Examine()/ExNext() enumerate them. Capped at
 * FS3ETHEMEVIEW_MAX_THEMES total entries (including the "-" one).
 * Selection resets to "-" afterward (does NOT call FS3EApp_LoadTheme --
 * simplest safe behavior, since a rescan can add/remove/reorder entries
 * out from under whatever index/theme was previously active/loaded; the
 * main window's currently loaded theme, if any, is left alone until the
 * user explicitly picks something from the freshly rebuilt list). */
static void FS3EThemeView_ScanThemes(FS3EThemeView *tv)
{
    BPTR                   lock;
    struct FileInfoBlock  *fib;

    if (!tv || !ChooserBase) return;

    clearThemeNodes(tv);
    addNoThemeNode(tv);

    lock = Lock((STRPTR)FS3ESTYLE_THEMES_ROOT, SHARED_LOCK);
    if (lock) {
        fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
        if (fib) {
            if (Examine(lock, fib)) {
                while (ExNext(lock, fib) && tv->themeCount < FS3ETHEMEVIEW_MAX_THEMES) {
                    if (fib->fib_DirEntryType >= 0 &&   /* directory, not a file */
                        hasStyleFile((const char *)fib->fib_FileName))
                    {
                        addScannedThemeNode(tv, (const char *)fib->fib_FileName);
                    }
                }
            }
            FreeDosObject(DOS_FIB, fib);
        }
        UnLock(lock);
    }

    if (tv->window) {
        SetGadgetAttrs((struct Gadget *)tv->themeChooser, tv->window, NULL,
            CHOOSER_Labels, (ULONG)&tv->themeList,
            CHOOSER_Active, 0UL,
            TAG_DONE);
        RefreshGList((struct Gadget *)tv->themeChooser, tv->window, NULL, 1);
    } else if (tv->themeChooser) {
        SetAttrs(tv->themeChooser,
            CHOOSER_Labels, (ULONG)&tv->themeList,
            CHOOSER_Active, 0UL,
            TAG_END);
    }
}

static Object *makeGetFileGadget(ULONG gadId, const char *initialPath)
{
    return NewObject(GETFILE_GetClass(), NULL,
        GA_ID,               gadId,
        GA_RelVerify,        TRUE,
        GETFILE_TitleText,   (ULONG)LOC(MSG_THEMEV_FONTS_GROUP),
        GETFILE_RejectIcons, TRUE,
        GETFILE_DoPatterns,  TRUE,
        GETFILE_Drawer,      (ULONG)"Fonts:",
        GETFILE_Pattern,     (ULONG)"#?(ttf|otf)",
        GETFILE_FilterDrawers, TRUE,
        GETFILE_ReadOnly,    FALSE,
        GETFILE_FullFileExpand, FALSE,
        (initialPath && initialPath[0]) ?
            GETFILE_File : TAG_IGNORE, (ULONG)initialPath,
        TAG_END);
}

static void syncCheckboxes(FS3EThemeView *tv)
{
    if (!tv->window) return;
    SetAttrs((struct Gadget *)tv->antialiasCheck,
             GA_Selected, (ULONG)(app->settings.antialias ? 1 : 0),
             TAG_END);
    SetAttrs((struct Gadget *)tv->emojiQualityCheck,
             GA_Selected, (ULONG)(app->settings.emojiQuality ? 1 : 0),
             TAG_END);
    RefreshGList((struct Gadget *)tv->antialiasCheck,   tv->window, NULL, 1);
    RefreshGList((struct Gadget *)tv->emojiQualityCheck, tv->window, NULL, 1);
}

static void syncFontPaths(FS3EThemeView *tv)
{
    if (!tv->window) return;
    SetGadgetAttrs((struct Gadget *)tv->primaryFontGF,    tv->window, NULL,
                   GETFILE_File, (ULONG)app->settings.primaryFontPath,    TAG_END);
    SetGadgetAttrs((struct Gadget *)tv->fallback1FontGF,  tv->window, NULL,
                   GETFILE_File, (ULONG)app->settings.fallback1FontPath,  TAG_END);
    SetGadgetAttrs((struct Gadget *)tv->fallback2FontGF,  tv->window, NULL,
                   GETFILE_File, (ULONG)app->settings.fallback2FontPath,  TAG_END);
    SetGadgetAttrs((struct Gadget *)tv->emojiFontGF,      tv->window, NULL,
                   GETFILE_File, (ULONG)app->settings.emojiFontPath,      TAG_END);
    SetGadgetAttrs((struct Gadget *)tv->colorEmojiFontGF, tv->window, NULL,
                   GETFILE_File, (ULONG)app->settings.colorEmojiFontPath, TAG_END);
}

static void removeAllFonts(void)
{
    FreeVec(app->settings.primaryFontPath);    app->settings.primaryFontPath    = NULL;
    FreeVec(app->settings.fallback1FontPath);  app->settings.fallback1FontPath  = NULL;
    FreeVec(app->settings.fallback2FontPath);  app->settings.fallback2FontPath  = NULL;
    FreeVec(app->settings.emojiFontPath);      app->settings.emojiFontPath      = NULL;
    FreeVec(app->settings.colorEmojiFontPath); app->settings.colorEmojiFontPath = NULL;
}

static void updateFontPath(Object *gf, char **dest)
{
    ULONG strPtr = 0;
    GetAttr(GETFILE_File, gf, &strPtr);
    if (strPtr) {
        const char *part = (const char *)FilePart((STRPTR)strPtr);
        FreeVec(*dest);
        *dest = ThemeStrDup(part);
        FS3EApp_ApplyFontSettings();
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

BOOL FS3EThemeView_Create(FS3EThemeView *tv, const char *title)
{
    Object *optionsGroup;
    Object *fontsGroup;
    Object *presetsGroup;
    Object *themeGroup;
    Object *antialiasLabel;
    Object *emojiQualityLabel;
    Object *primaryLabel;
    Object *fallback1Label;
    Object *fallback2Label;
    Object *emojiFontLabel;
    Object *colorEmojiFontLabel;
    Object *themeLabel;

    {
        LONG sl = tv->left, st = tv->top, sw = tv->width, sh = tv->height;
        memset(tv, 0, sizeof(*tv));
        tv->left = sl; tv->top = st; tv->width = sw; tv->height = sh;
    }

    /* --- Checkboxes (options group) --- */
    tv->antialiasCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_THEMEV_ANTIALIAS,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.antialias,
        TAG_END);
    if (!tv->antialiasCheck) return FALSE;

    antialiasLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_THEMEV_ANTIALIAS),
        TAG_END);

    tv->emojiQualityCheck = NewObject(CHECKBOX_GetClass(), NULL,
        GA_ID,        GID_THEMEV_EMOJIQUALITY,
        GA_RelVerify, TRUE,
        GA_Selected,  (ULONG)app->settings.emojiQuality,
        TAG_END);
    if (!tv->emojiQualityCheck) return FALSE;

    emojiQualityLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_THEMEV_EMOJIQUALITY),
        TAG_END);

    optionsGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_THEMEV_OPTIONS_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)tv->antialiasCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)antialiasLabel,
        LAYOUT_AddChild,      (ULONG)tv->emojiQualityCheck,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)emojiQualityLabel,
        TAG_END);
    if (!optionsGroup) return FALSE;

    /* --- Getfile gadgets --- */
    tv->primaryFontGF    = makeGetFileGadget(GID_THEMEV_PRIMARY_FONT,     app->settings.primaryFontPath);
    tv->fallback1FontGF  = makeGetFileGadget(GID_THEMEV_FALLBACK1_FONT,   app->settings.fallback1FontPath);
    tv->fallback2FontGF  = makeGetFileGadget(GID_THEMEV_FALLBACK2_FONT,   app->settings.fallback2FontPath);
    tv->emojiFontGF      = makeGetFileGadget(GID_THEMEV_EMOJI_FONT,       app->settings.emojiFontPath);
    tv->colorEmojiFontGF = makeGetFileGadget(GID_THEMEV_COLOR_EMOJI_FONT, app->settings.colorEmojiFontPath);
    if (!tv->primaryFontGF || !tv->fallback1FontGF || !tv->fallback2FontGF ||
        !tv->emojiFontGF   || !tv->colorEmojiFontGF) return FALSE;

    primaryLabel   = NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)LOC(MSG_THEMEV_PRIMARY),        TAG_END);
    fallback1Label = NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)LOC(MSG_THEMEV_FALLBACK1),      TAG_END);
    fallback2Label = NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)LOC(MSG_THEMEV_FALLBACK2),      TAG_END);
    emojiFontLabel      = NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)LOC(MSG_THEMEV_EMOJIFONT),      TAG_END);
    colorEmojiFontLabel = NewObject(LABEL_GetClass(), NULL, LABEL_Text, (ULONG)LOC(MSG_THEMEV_COLOREMOJIFONT), TAG_END);

    /* --- [getfile][X] row sub-layouts (owned by fontsGroup) --- */
    {
        Object *primaryRow, *fallback1Row, *fallback2Row, *emojiFontRow, *colorEmojiFontRow;
        Object *primaryClearBtn, *fallback1ClearBtn, *fallback2ClearBtn, *emojiClearBtn, *colorEmojiClearBtn;

        primaryClearBtn   = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_PRIMARY_CLEAR,   GA_RelVerify, TRUE, GA_Text, (ULONG)"X", TAG_END);
        fallback1ClearBtn = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_FALLBACK1_CLEAR, GA_RelVerify, TRUE, GA_Text, (ULONG)"X", TAG_END);
        fallback2ClearBtn = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_FALLBACK2_CLEAR, GA_RelVerify, TRUE, GA_Text, (ULONG)"X", TAG_END);
        emojiClearBtn       = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_EMOJI_CLEAR,       GA_RelVerify, TRUE, GA_Text, (ULONG)"X", TAG_END);
        colorEmojiClearBtn  = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_COLOR_EMOJI_CLEAR, GA_RelVerify, TRUE, GA_Text, (ULONG)"X", TAG_END);
        if (!primaryClearBtn || !fallback1ClearBtn || !fallback2ClearBtn ||
            !emojiClearBtn   || !colorEmojiClearBtn)
            return FALSE;

        primaryRow = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,    BVS_NONE,
            LAYOUT_SpaceInner,    FALSE,
            LAYOUT_AddChild,      (ULONG)tv->primaryFontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,      (ULONG)primaryClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!primaryRow) return FALSE;

        fallback1Row = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,    BVS_NONE,
            LAYOUT_SpaceInner,    FALSE,
            LAYOUT_AddChild,      (ULONG)tv->fallback1FontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,      (ULONG)fallback1ClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!fallback1Row) return FALSE;

        fallback2Row = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,    BVS_NONE,
            LAYOUT_SpaceInner,    FALSE,
            LAYOUT_AddChild,      (ULONG)tv->fallback2FontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,      (ULONG)fallback2ClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!fallback2Row) return FALSE;

        emojiFontRow = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,    BVS_NONE,
            LAYOUT_SpaceInner,    FALSE,
            LAYOUT_AddChild,      (ULONG)tv->emojiFontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,      (ULONG)emojiClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!emojiFontRow) return FALSE;

        colorEmojiFontRow = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
            LAYOUT_BevelStyle,    BVS_NONE,
            LAYOUT_SpaceInner,    FALSE,
            LAYOUT_AddChild,      (ULONG)tv->colorEmojiFontGF,
            CHILD_WeightedWidth,  1,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,      (ULONG)colorEmojiClearBtn,
            CHILD_WeightedWidth,  0,
            CHILD_WeightedHeight, 0,
            TAG_END);
        if (!colorEmojiFontRow) return FALSE;

        fontsGroup = NewObject(LAYOUT_GetClass(), NULL,
            LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
            LAYOUT_BevelStyle,    BVS_GROUP,
            LAYOUT_Label,         (ULONG)LOC(MSG_THEMEV_FONTS_GROUP),
            LAYOUT_BackFill,      NULL,
            LAYOUT_SpaceOuter,    TRUE,
            LAYOUT_SpaceInner,    TRUE,
            LAYOUT_AddChild,      (ULONG)primaryRow,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)primaryLabel,
            LAYOUT_AddChild,      (ULONG)fallback1Row,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)fallback1Label,
            LAYOUT_AddChild,      (ULONG)fallback2Row,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)fallback2Label,
            LAYOUT_AddChild,      (ULONG)emojiFontRow,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)emojiFontLabel,
            LAYOUT_AddChild,      (ULONG)colorEmojiFontRow,
            CHILD_WeightedHeight, 0,
            CHILD_Label,          (ULONG)colorEmojiFontLabel,
            TAG_END);
        if (!fontsGroup) return FALSE;
    }

    /* --- Preset buttons --- */
    tv->presetLowBtn  = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_PRESET_LOW,  GA_RelVerify, TRUE, GA_Text, (ULONG)LOC(MSG_THEMEV_PRESET_LOW),  TAG_END);
    tv->presetHQBtn   = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_PRESET_HQ,   GA_RelVerify, TRUE, GA_Text, (ULONG)LOC(MSG_THEMEV_PRESET_HQ),   TAG_END);
    tv->presetMonoBtn = NewObject(BUTTON_GetClass(), NULL, GA_ID, GID_THEMEV_PRESET_MONO, GA_RelVerify, TRUE, GA_Text, (ULONG)LOC(MSG_THEMEV_PRESET_MONO), TAG_END);
    if (!tv->presetLowBtn || !tv->presetHQBtn || !tv->presetMonoBtn) return FALSE;

    presetsGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_HORIZ,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_THEMEV_PRESETS_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)tv->presetLowBtn,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)tv->presetHQBtn,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)tv->presetMonoBtn,
        CHILD_WeightedWidth,  1,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (!presetsGroup) return FALSE;

    /* --- Theme chooser --- */
    NewList(&tv->themeList);
    tv->themeCount = 0;
    addNoThemeNode(tv);

    /* If a theme is currently activated (app->settings.themeName, restored
     * at startup by FS3EApp_LoadTheme in friendsh3ep.c) and it still
     * actually exists on disk, seed it as the one other entry so the
     * chooser reflects what's really loaded on the main window right now
     * -- without a full FS3EThemeView_ScanThemes() rescan, which the user
     * has to ask for explicitly (Scan Themes button). */
    {
        ULONG initialActive = 0;
        if (app->settings.themeName && app->settings.themeName[0] &&
            hasStyleFile(app->settings.themeName))
        {
            addScannedThemeNode(tv, app->settings.themeName);
            initialActive = (ULONG)(tv->themeCount - 1);
        }

        tv->themeChooser = NewObject(CHOOSER_GetClass(), NULL,
            GA_ID,          GID_THEMEV_THEME_CHOOSER,
            GA_RelVerify,   TRUE,
            CHOOSER_PopUp,  TRUE,
            CHOOSER_Labels, (ULONG)&tv->themeList,
            CHOOSER_Active, initialActive,
            TAG_END);
        if (!tv->themeChooser) return FALSE;
    }

    themeLabel = NewObject(LABEL_GetClass(), NULL,
        LABEL_Text, (ULONG)LOC(MSG_THEMEV_THEME_NAME),
        TAG_END);

    /* --- Scan Themes button -- sits above the chooser, populates it --- */
    tv->scanThemesBtn = NewObject(BUTTON_GetClass(), NULL,
        GA_ID,        GID_THEMEV_SCAN_THEMES,
        GA_RelVerify, TRUE,
        GA_Text,      (ULONG)LOC(MSG_THEMEV_SCAN_THEMES),
        TAG_END);
    if (!tv->scanThemesBtn) return FALSE;

    themeGroup = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_GROUP,
        LAYOUT_Label,         (ULONG)LOC(MSG_THEMEV_THEME_GROUP),
        LAYOUT_BackFill,      NULL,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)tv->scanThemesBtn,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)tv->themeChooser,
        CHILD_WeightedHeight, 0,
        CHILD_Label,          (ULONG)themeLabel,
        TAG_END);
    if (!themeGroup) return FALSE;

    /* --- Top-level vertical layout --- */
    tv->mainLayout = NewObject(LAYOUT_GetClass(), NULL,
        LAYOUT_DeferLayout,   TRUE,
        LAYOUT_Orientation,   LAYOUT_ORIENT_VERT,
        LAYOUT_BevelStyle,    BVS_NONE,
        LAYOUT_SpaceOuter,    TRUE,
        LAYOUT_SpaceInner,    TRUE,
        LAYOUT_AddChild,      (ULONG)optionsGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)fontsGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)presetsGroup,
        CHILD_WeightedHeight, 0,
        LAYOUT_AddChild,      (ULONG)themeGroup,
        CHILD_WeightedHeight, 0,
        TAG_END);
    if (!tv->mainLayout) return FALSE;

    /* --- BOOPSI window object --- */
    tv->windowObj = NewObject(WINDOW_GetClass(), NULL,
        WA_Left,  120,
        WA_Top,   80,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_CLOSEGADGET | WFLG_ACTIVATE | WFLG_SIZEGADGET |
                  WFLG_SMART_REFRESH,
        WA_Title, (ULONG)title,
        WINDOW_ParentGroup, (ULONG)tv->mainLayout,
        TAG_END);
    if (!tv->windowObj) {
        DisposeObject(tv->mainLayout);
        tv->mainLayout = NULL;
        return FALSE;
    }

    return TRUE;
}

void FS3EThemeView_Dispose(FS3EThemeView *tv)
{
    int i;
    if (!tv) return;

    if (tv->windowObj) {
        FS3EThemeView_Close(tv);
        DisposeObject(tv->windowObj);
        tv->windowObj = NULL;
    }

    if (ChooserBase) {
        for (i = 0; i < tv->themeCount; i++) {
            if (tv->themeNodes[i]) {
                FreeChooserNode(tv->themeNodes[i]);
                tv->themeNodes[i] = NULL;
            }
            FreeVec(tv->themeNames[i]);
            tv->themeNames[i] = NULL;
        }
    }
    tv->themeCount = 0;
}

void FS3EThemeView_Open(FS3EThemeView *tv)
{
    if (!tv || !tv->windowObj) return;

    if (tv->window) {
        WindowToFront(tv->window);
        ActivateWindow(tv->window);
        return;
    }

    if (CurrentMainScreen) {
        SetAttrs(tv->windowObj,
                 WA_CustomScreen, (ULONG)CurrentMainScreen,
                 TAG_END);
    }

    if (tv->width > 0) {
        SetAttrs(tv->windowObj,
                 WA_Left,   (ULONG)tv->left,
                 WA_Top,    (ULONG)tv->top,
                 WA_Width,  (ULONG)tv->width,
                 WA_Height, (ULONG)tv->height,
                 TAG_END);
    }

    tv->window = (struct Window *)DoMethod(tv->windowObj, WM_OPEN, NULL);
}

void FS3EThemeView_Close(FS3EThemeView *tv)
{
    if (!tv || !tv->windowObj || !tv->window) return;

    GetAttr(WA_Left,   tv->windowObj, (ULONG *)&tv->left);
    GetAttr(WA_Top,    tv->windowObj, (ULONG *)&tv->top);
    GetAttr(WA_Width,  tv->windowObj, (ULONG *)&tv->width);
    GetAttr(WA_Height, tv->windowObj, (ULONG *)&tv->height);

    FS3ESettings_Save(&app->settings);
    DoMethod(tv->windowObj, WM_CLOSE, NULL);
    tv->window = NULL;
}

BOOL FS3EThemeView_HandleInput(FS3EThemeView *tv)
{
    ULONG result;

    if (!tv || !tv->windowObj) return FALSE;
    if (!tv->window) return TRUE;

    while ((result = DoMethod(tv->windowObj, WM_HANDLEINPUT, NULL))
           != WMHI_LASTMSG)
    {
        switch (result & WMHI_CLASSMASK)
        {
            case WMHI_CLOSEWINDOW:
                FS3EThemeView_Close(tv);
                return TRUE;

            case WMHI_RAWKEY:
            {
                ULONG key = result & 0x07f;
                ULONG isUp = (result & 0x080);

                if (key == 0x45 && isUp) { /* Esc */
                    FS3EThemeView_Close(tv);
                    return TRUE;
                }
            }
                break;

            case WMHI_GADGETUP:
            {
                ULONG gadId = result & WMHI_GADGETMASK;

                if (gadId == GID_THEMEV_ANTIALIAS) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, tv->antialiasCheck, &checked);
                    app->settings.antialias = checked ? TRUE : FALSE;
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_EMOJIQUALITY) {
                    ULONG checked = 0;
                    GetAttr(GA_Selected, tv->emojiQualityCheck, &checked);
                    app->settings.emojiQuality = checked ? TRUE : FALSE;
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_PRIMARY_FONT) {
                    if (gfRequestFile(tv->primaryFontGF, tv->window))
                        updateFontPath(tv->primaryFontGF, &app->settings.primaryFontPath);

                } else if (gadId == GID_THEMEV_FALLBACK1_FONT) {
                    if (gfRequestFile(tv->fallback1FontGF, tv->window))
                        updateFontPath(tv->fallback1FontGF, &app->settings.fallback1FontPath);

                } else if (gadId == GID_THEMEV_FALLBACK2_FONT) {
                    if (gfRequestFile(tv->fallback2FontGF, tv->window))
                        updateFontPath(tv->fallback2FontGF, &app->settings.fallback2FontPath);

                } else if (gadId == GID_THEMEV_EMOJI_FONT) {
                    if (gfRequestFile(tv->emojiFontGF, tv->window))
                        updateFontPath(tv->emojiFontGF, &app->settings.emojiFontPath);

                } else if (gadId == GID_THEMEV_COLOR_EMOJI_FONT) {
                    if (gfRequestFile(tv->colorEmojiFontGF, tv->window))
                        updateFontPath(tv->colorEmojiFontGF, &app->settings.colorEmojiFontPath);

                } else if (gadId == GID_THEMEV_PRIMARY_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)tv->primaryFontGF,
                                   tv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->settings.primaryFontPath);
                    app->settings.primaryFontPath = NULL;
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_FALLBACK1_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)tv->fallback1FontGF,
                                   tv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->settings.fallback1FontPath);
                    app->settings.fallback1FontPath = NULL;
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_FALLBACK2_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)tv->fallback2FontGF,
                                   tv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->settings.fallback2FontPath);
                    app->settings.fallback2FontPath = NULL;
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_EMOJI_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)tv->emojiFontGF,
                                   tv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->settings.emojiFontPath);
                    app->settings.emojiFontPath = NULL;
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_COLOR_EMOJI_CLEAR) {
                    SetGadgetAttrs((struct Gadget *)tv->colorEmojiFontGF,
                                   tv->window, NULL,
                                   GETFILE_File,   (ULONG)"",
                                   GETFILE_Drawer, (ULONG)"",
                                   TAG_END);
                    FreeVec(app->settings.colorEmojiFontPath);
                    app->settings.colorEmojiFontPath = NULL;
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_PRESET_LOW) {
                    app->settings.antialias    = FALSE;
                    app->settings.emojiQuality = FALSE;
                    syncCheckboxes(tv);
                    removeAllFonts();
                    app->settings.primaryFontPath    = ThemeStrDup("LiberationSans-Regular.ttf");
                    app->settings.emojiFontPath      = ThemeStrDup("OpenMoji-black-glyf.ttf");
                    app->settings.colorEmojiFontPath = ThemeStrDup("OpenMoji-black-glyf.ttf");
                    syncFontPaths(tv);
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_PRESET_HQ) {
                    app->settings.antialias    = TRUE;
                    app->settings.emojiQuality = TRUE;
                    syncCheckboxes(tv);
                    removeAllFonts();
                    app->settings.primaryFontPath    = ThemeStrDup("LiberationSans-Regular.ttf");
                    app->settings.emojiFontPath      = ThemeStrDup("OpenMoji-black-glyf.ttf");
                    app->settings.colorEmojiFontPath = ThemeStrDup("NotoColorEmoji32.ttf");
                    syncFontPaths(tv);
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_PRESET_MONO) {
                    app->settings.antialias    = TRUE;
                    app->settings.emojiQuality = TRUE;
                    syncCheckboxes(tv);
                    removeAllFonts();
                    app->settings.primaryFontPath    = ThemeStrDup("UbuntuMono-Regular.ttf");
                    app->settings.emojiFontPath      = ThemeStrDup("OpenMoji-black-glyf.ttf");
                    app->settings.colorEmojiFontPath = ThemeStrDup("NotoColorEmoji32.ttf");
                    syncFontPaths(tv);
                    FS3EApp_ApplyFontSettings();

                } else if (gadId == GID_THEMEV_SCAN_THEMES) {
                    FS3EThemeView_ScanThemes(tv);

                } else if (gadId == GID_THEMEV_THEME_CHOOSER) {
                    /* Theme selection: persist to settings.themeName (NULL
                     * for index 0, "-"/no theme) and apply it to the main
                     * window right away. Saved to disk when this window
                     * closes, same as every other field here (see
                     * FS3EThemeView_Close's FS3ESettings_Save call). */
                    ULONG active = 0;
                    const char *name = NULL;

                    GetAttr(CHOOSER_Active, tv->themeChooser, &active);
                    if ((int)active >= 0 && (int)active < tv->themeCount)
                        name = tv->themeNames[active];

                    FreeVec(app->settings.themeName);
                    app->settings.themeName = name ? ThemeStrDup(name) : NULL;

                   //was test flushThemeImagesFromButtons();
                    FS3EApp_LoadTheme(app->settings.themeName);
                }
                break;
            }

            default:
                break;
        }
    }

    return TRUE;
}

ULONG FS3EThemeView_GetSignalMask(FS3EThemeView *tv)
{
    if (!tv || !tv->window) return 0;
    return (1L << tv->window->UserPort->mp_SigBit);
}
