/*
 * Eglocale.c – localization support for Eg UTF-8 text editor.
 */

#include "eglocale.h"
#include <stdio.h>
#include <proto/locale.h>
#include <libraries/locale.h>

/*
 * Default English strings – MUST stay in the same order as the MSG_* enum.
 */
static const char *defaultStrings[MSG_COUNT] = {
    /* MSG_WINDOW_TITLE */
    "EmojiGear - Unicode Text Editor",

    /* MSG_WINDOW_TITLE_WITH_FILE */
    "EmojiGear - %s",

    /* MSG_MENU_PROJECT */
    "Project",
    /* MSG_FILE_NEW */
    "New",
    /* MSG_FILE_LOAD_UTF8 */
    "Load UTF-8 Text...",
    /* MSG_FILE_LOAD_LATIN1 */
    "Load ISO Latin-1 Text (en,fr,de,es,it)...",
    /* MSG_FILE_LOAD_LATIN2 */
    "Load ISO Latin-2 Text (cs,pl,sk,sl,hr)...",

    /* MSG_FILE_CLOSE */
    "Close",

    /* MSG_FILE_SAVE */
    "Save",
    /* MSG_FILE_SAVE_ALL */
    "Save All",
    /* MSG_FILE_SAVE_UTF8 */
    "Save As UTF-8 Text...",
    /* MSG_FILE_SAVE_ESCAPED */
    "Save UTF-8 As Escaped ASCII...",
    /* MSG_FILE_SAVE_LATIN1 */
    "Save As ISO Latin-1 Text (en,fr,de,es,it)...",
    /* MSG_FILE_SAVE_LATIN2 */
    "Save As ISO Latin-2 Text (cs,pl,sk,sl,hr)...",
    /* MSG_FILE_ABOUT */
    "About EmojiGear...",
    /* MSG_MENU_QUIT */
    "Quit",

    /* MSG_CLOSE_CONFIRM_TITLE */
    "EmojiGear - Close",
    /* MSG_CLOSE_CONFIRM_BODY */
    "This file has unsaved changes:\n%s\n\nSave before closing?",
    /* MSG_CLOSE_CONFIRM_GADGETS */
    "Save|Don't Save",
    /* MSG_QUIT_CONFIRM_TITLE */
    "EmojiGear - Quit",
    /* MSG_QUIT_CONFIRM_BODY */
    "One or more files have unsaved changes.\n\nSave them all before quitting?",
    /* MSG_QUIT_CONFIRM_GADGETS */
    "Save All|Quit Without Saving",

    /* MSG_STATUS_READY */
    "Ready",
    /* MSG_STATUS_MODIFIED */
    "Modified",
    /* MSG_STATUS_SAVED */
    "Saved: %s",
    /* MSG_STATUS_SAVE_FAILED */
    "Save failed: %s",
    /* MSG_STATUS_SAVE_ALL_NONE */
    "Nothing to save",
    /* MSG_STATUS_SAVE_ALL_DONE */
    "Saved %ld/%ld file(s)",

    /* MSG_ERROR_NOMEMORY */
    "Out of memory",
    /* MSG_ERROR_OPENFILE */
    "Cannot open file",
    /* MSG_ERROR_SAVEFILE */
    "Cannot save file",
    /* MSG_ERROR_MENU */
    "Menu creation failed",

    /* MSG_MENU_EDIT */
    "Edit",
    /* MSG_EDIT_CUT */
    "Cut",
    /* MSG_EDIT_COPY */
    "Copy UTF-8",
    /* MSG_EDIT_COPY_LATIN1 */
    "Copy As ISO Latin-1 (en,fr,de,es,it)",
    /* MSG_EDIT_COPY_LATIN2 */
    "Copy As ISO Latin-2 (cs,pl,sk,sl,hr)",
    /* MSG_EDIT_PASTE */
    "Paste UTF-8",
    /* MSG_EDIT_PASTE_LATIN1 */
    "Paste from ISO Latin-1 (en,fr,de,es,it)",
    /* MSG_EDIT_PASTE_LATIN2 */
    "Paste from ISO Latin-2 (cs,pl,sk,sl,hr)",
    /* MSG_EDIT_UNDO */
    "Undo",
    /* MSG_EDIT_REDO */
    "Redo",
    /* MSG_EDIT_EMOJIBOX */
    "Emoji Box...",

    /*MSG_MENU_NAVIGATE,*/
    "Navigate",
    /*MSG_NAV_FIND,*/
    "Find...",
    /*MSG_NAV_FINDANDR,*/
    "Find & Replace...",
    /*MSG_NAV_FINDNEXT,*/
    "Find Next",
    /*MSG_NAV_FINDPREV,*/
    "Find Previous",
    /*MSG_NAV_GOTOL,*/
    "Go to Line...",

    /*MSG_GOTOL_TITLE*/
    "Go to Line",
    /*MSG_GOTOL_PROMPT*/
    "Enter line number (1-%lu):",
    /*MSG_GOTOL_OK*/
    "OK",
    /*MSG_GOTOL_CANCEL,*/
    "Cancel",

     /*MSG_NAV_PREVTEXT*/
    "Previous Text",
    /*MSG_NAV_NEXTTEXT*/
    "Next Text",

    /* MSG_SETTINGS */
    "Settings",

    /* MSG_SETTINGS_DOTS */
    "Settings...",

    "Word Wrap",
    "Force Monospace",
    "ANSI Escape Rendering",
    "Use ANSI Unix Colors",
    "Display Unicode Info",
    "Font Size +",
    "Font Size -",

    /* MSG_SETTINGS_EDITORDISPLAY */
    "Editor display:",
    /* MSG_SETTINGS_EDITORBGCOLOR */
    "Background Color",
    /* MSG_SETTINGS_EDITORPENCOLOR */
    "Pen Color",
    /* MSG_SETTINGS_TABSPACES */
    "Tab Spaces",
    /* MSG_SETTINGS_VISUALIZETABS */
    "Visualize Tabs",
    /* MSG_SETTINGS_TABSARESPACES */
    "Tab key does spaces",

    /* MSG_OPEN_RECENT */
    "Open Recent",

    /* MSG_SEARCH_LABEL */
    "Search :",
    /* MSG_REPLACE_LABEL */
    "Replace :",
    /* MSG_SEARCH_ERASE */
    "X",
    /* MSG_SEARCH_FIND_NEXT */
    "Find Next",
    /* MSG_SEARCH_FIND_PREV */
    "Find Previous",
    /* MSG_SEARCH_REPLACE */
    "Replace",
    /* MSG_SEARCH_REPLACE_ALL */
    "Replace All",
    /* MSG_SEARCH_CASE_SENS */
    "Case sensitive",
    /* MSG_SEARCH_CLOSE */
    "Close",

    /* MSG_FONTSETTINGS */
    "Font Settings",
    /* MSG_FONTSETTINGS_DOTS */
    "Font Settings...",
    /* MSG_FONTSETTINGS_ANTIALIAS */
    "Antialias when possible",
    /* MSG_FONTSETTINGS_EMOJIQUALITY */
    "Emoji Quality scaling",
    /* MSG_FONTSETTINGS_OPTIONS_GROUP */
    "Options",
    /* MSG_FONTSETTINGS_FONTS_GROUP */
    "Fonts",
    /* MSG_FONTSETTINGS_PRIMARY */
    "Primary Font:",
    /* MSG_FONTSETTINGS_FALLBACK1 */
    "Fallback Font 1:",
    /* MSG_FONTSETTINGS_FALLBACK2 */
    "Fallback Font 2:",
    /* MSG_FONTSETTINGS_EMOJIFONT */
    "Emoji Font:",
    /* MSG_FONTSETTINGS_PRESETS_GROUP */
    "Presets",
    /* MSG_FONTSETTINGS_PRESET_LOW */
    "Low-end 2 colors",
    /* MSG_FONTSETTINGS_PRESET_HQ */
    "High Quality",
    /* MSG_FONTSETTINGS_PRESET_MONO */
    "High Monospace",

    /* MSG_TAB_NEW_FILE */
    "New File",

    /* MSG_FILE_EXTERNAL_CHANGE_TITLE */
    "EmojiGear - File Changed",
    /* MSG_FILE_EXTERNAL_CHANGE_BODY */
    "This file was changed by another program:\n%s\n\nReload it from disk? Unsaved changes in this tab will be lost.",
    /* MSG_FILE_EXTERNAL_CHANGE_GADGETS */
    "Reload|Cancel",

};

/* LocaleBase declared in main.c */
extern struct LocaleBase *LocaleBase;

static struct Catalog *catalog = NULL;

BOOL EgLocale_Init(const char *catalogName, ULONG version)
{
    if (LocaleBase && catalogName) {
        catalog = OpenCatalog(NULL, (STRPTR)catalogName,
                              OC_Version, version,
                              OC_BuiltInLanguage, (ULONG)"english",
                              TAG_DONE);
    }
    return TRUE;
}

void EgLocale_Close(void)
{
    if (catalog) {
        CloseCatalog(catalog);
        catalog = NULL;
    }
}

const char *EgLocale_GetString(ULONG stringID)
{
    if (stringID >= MSG_COUNT) return "???";

    if (LocaleBase && catalog)
        return GetCatalogStr(catalog, stringID, (STRPTR)defaultStrings[stringID]);

    return defaultStrings[stringID];
}
