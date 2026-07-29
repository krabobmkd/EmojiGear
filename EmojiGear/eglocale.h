/*
 * Eglocale.h – localization support for Eg UTF-8 text editor.
 *
 * Uses locale.library catalog if available, falls back to built-in
 * English strings.  LocaleBase must be declared in main.c.
 */

#ifndef EgLOCALE_H
#define EgLOCALE_H

#include <exec/types.h>

/* String IDs – must stay in the same order as defaultStrings[] in Eglocale.c */
enum {
    /* Window */
    MSG_WINDOW_TITLE = 0, /* default window title */
    MSG_WINDOW_TITLE_WITHFILE,  /*  window title with %s for file name */

    /* Menu: Project */
    MSG_MENU_PROJECT,
    MSG_FILE_NEW,
    MSG_FILE_LOAD_UTF8,
    MSG_FILE_LOAD_LATIN1,
    MSG_FILE_LOAD_LATIN2,

    MSG_FILE_CLOSE,

    MSG_FILE_SAVE,
    MSG_FILE_SAVE_ALL,
    MSG_FILE_SAVE_UTF8,
    MSG_FILE_SAVE_ESCAPED,
    MSG_FILE_SAVE_LATIN1,
    MSG_FILE_SAVE_LATIN2,
    MSG_FILE_ABOUT,
    MSG_MENU_QUIT,

    /* Close/Quit confirmation requesters */
    MSG_CLOSE_CONFIRM_TITLE,
    MSG_CLOSE_CONFIRM_BODY,    /* format: %s = file path */
    MSG_CLOSE_CONFIRM_GADGETS,
    MSG_QUIT_CONFIRM_TITLE,
    MSG_QUIT_CONFIRM_BODY,
    MSG_QUIT_CONFIRM_GADGETS,

    /* Status bar */
    MSG_STATUS_READY,
    MSG_STATUS_MODIFIED,
    MSG_STATUS_SAVED,       /* format: "Saved: %s" */
    MSG_STATUS_SAVE_FAILED, /* format: "Save failed: %s" */
    MSG_STATUS_SAVE_ALL_NONE, /* nothing modified/on-disk to save */
    MSG_STATUS_SAVE_ALL_DONE, /* format: "Saved %ld/%ld file(s)" */

    /* Error messages */
    MSG_ERROR_NOMEMORY,
    MSG_ERROR_OPENFILE,
    MSG_ERROR_SAVEFILE,
    MSG_ERROR_MENU,

    /* Menu: Edit */
    MSG_MENU_EDIT,
    MSG_EDIT_CUT,
    MSG_EDIT_COPY,
    MSG_EDIT_COPY_LATIN1,
    MSG_EDIT_COPY_LATIN2,
    MSG_EDIT_PASTE,
    MSG_EDIT_PASTE_LATIN1,
    MSG_EDIT_PASTE_LATIN2,
    MSG_EDIT_UNDO,
    MSG_EDIT_REDO,
    MSG_EDIT_EMOJIBOX,

    /**/
    MSG_MENU_NAVIGATE,
    MSG_NAV_FIND,
    MSG_NAV_FINDANDR,
    MSG_NAV_FINDNEXT,
    MSG_NAV_FINDPREV,
    MSG_NAV_GOTOL,

    MSG_GOTOL_TITLE,
    MSG_GOTOL_PROMPT,  /* format: "Enter line number (1-%lu):" */
    MSG_GOTOL_OK,
    MSG_GOTOL_CANCEL,

    MSG_NAV_PREVTEXT,
    MSG_NAV_NEXTTEXT,
    /* */
    MSG_SETTINGS,

    MSG_SETTINGS_DOTS,

    MSG_SETTINGS_WORDWRAP,
    MSG_SETTINGS_FORCEMONOSPACE,
    MSG_SETTINGS_APPLYANSI,
    MSG_SETTINGS_ANSI_UNIX_COLORS,
    MSG_SETTINGS_DISPLAYUNICODEINFO,
    MSG_SETTING_FONTSIZEP,
    MSG_SETTING_FONTSIZEM,

    /* editor display panel */
    MSG_SETTINGS_EDITORDISPLAY,
    MSG_SETTINGS_EDITORBGCOLOR,
    MSG_SETTINGS_EDITORPENCOLOR,
    MSG_SETTINGS_TABSPACES,
    MSG_SETTINGS_VISUALIZETABS,
    MSG_SETTINGS_TABSARESPACES,

    /* Project menu: Open Recent */
    MSG_OPEN_RECENT,

    /* Search & Replace box */
    MSG_SEARCH_LABEL,
    MSG_REPLACE_LABEL,
    MSG_SEARCH_ERASE,
    MSG_SEARCH_FIND_NEXT,
    MSG_SEARCH_FIND_PREV,
    MSG_SEARCH_REPLACE,
    MSG_SEARCH_REPLACE_ALL,
    MSG_SEARCH_CASE_SENS,
    MSG_SEARCH_CLOSE,

    /* Font Settings window */
    MSG_FONTSETTINGS,
    MSG_FONTSETTINGS_DOTS,
    MSG_FONTSETTINGS_ANTIALIAS,
    MSG_FONTSETTINGS_EMOJIQUALITY,
    MSG_FONTSETTINGS_OPTIONS_GROUP,
    MSG_FONTSETTINGS_FONTS_GROUP,
    MSG_FONTSETTINGS_PRIMARY,
    MSG_FONTSETTINGS_FALLBACK1,
    MSG_FONTSETTINGS_FALLBACK2,
    MSG_FONTSETTINGS_EMOJIFONT,
    MSG_FONTSETTINGS_PRESETS_GROUP,
    MSG_FONTSETTINGS_PRESET_LOW,
    MSG_FONTSETTINGS_PRESET_HQ,
    MSG_FONTSETTINGS_PRESET_MONO,




    /* Tab bar */
    MSG_TAB_NEW_FILE,

    /* External file-change notification (dos.library StartNotify) */
    MSG_FILE_EXTERNAL_CHANGE_TITLE,
    MSG_FILE_EXTERNAL_CHANGE_BODY,    /* RawDoFmt style: %s for the file path */
    MSG_FILE_EXTERNAL_CHANGE_GADGETS,

    /* Must be last */
    MSG_COUNT
};

/* Initialize locale system.  catalogName may be NULL (English only). */
BOOL EgLocale_Init(const char *catalogName, ULONG version);

/* Close catalog and free resources. */
void EgLocale_Close(void);

/* Return localized string by ID.  Falls back to built-in English. */
const char *EgLocale_GetString(ULONG stringID);

/* Convenience macro */
#define LOC(id) EgLocale_GetString(id)

#endif /* EgLOCALE_H */
