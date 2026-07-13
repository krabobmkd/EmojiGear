/*
 * fs3elocale.c - localization support for FriendSh3ep.
 *
 * Pattern adapted from EmojiGear/eglocale.c.
 */

#include "fs3elocale.h"
#include <proto/locale.h>
#include <libraries/locale.h>

/*
 * Default English strings - MUST stay in the same order as the MSG_* enum.
 */
static const char *defaultStrings[MSG_COUNT] = {
    /* MSG_WINDOW_TITLE */
    "FriendSh3ep - Mastodon client (work in progress)",
    /* MSG_LOGIN_BUTTON */
    "Login...",
    /* MSG_NEWTOOT_BUTTON */
    "New toot...",

    /* MSG_LOGIN_TITLE */
    "Login to Mastodon",
    /* MSG_LOGIN_SERVER */
    "Server",
    /* MSG_LOGIN_USER */
    "User",
    "User or mail",
    /* MSG_LOGIN_CODE */
    "Code",
    /* MSG_LOGIN_LOGIN */
    "Login",

    /* MSG_TOOT_TITLE */
    "Toot",
    /* MSG_TOOT_CONTEXT_NEW */
    "Creating a new toot",
    /* MSG_TOOT_CONTEXT_MODIFY */
    "Modify your toot",
    /* MSG_TOOT_CONTEXT_POLL */
    "Creating a new poll",
    /* MSG_TOOT_CONTEXT_REPLY_FORMAT */
    "Reply to %s's toot\n\nRemember to stay polite and calm,\n and avoid sarcasm.\n -- tone doesn't always come across in text.\nRemember you can be a better person.\xE2\x9D\xA4",
    /* MSG_TOOT_VISIBILITY_PUBLIC */
    "Public",
    /* MSG_TOOT_VISIBILITY_UNLISTED */
    "Unlisted",
    /* MSG_TOOT_VISIBILITY_PRIVATE */
    "Private",
    /* MSG_TOOT_VISIBILITY_DIRECT */
    "Direct",
    /* MSG_TOOT_SEND */
    "Toot",
    /* MSG_TOOT_SEND_MODIFY */
    "Modify",
    /* MSG_TOOT_SEND_REPLY */
    "Reply",
    /* MSG_TOOT_CHARS_FORMAT */
    "%lu / Max: %s",

    /* MSG_TOOTMENU_TOOT */
    "Toot",
    /* MSG_TOOTMENU_CLEAR */
    "Clear",
    /* MSG_TOOTMENU_UNDO */
    "Undo",
    /* MSG_TOOTMENU_REDO */
    "Redo",
    /* MSG_TOOTMENU_CUT */
    "Cut",
    /* MSG_TOOTMENU_COPY */
    "Copy UTF8",
    /* MSG_TOOTMENU_PASTE */
    "Paste UTF8",
    /* MSG_TOOTMENU_EMOJIBOX */
    "Emoji Box",

    /* MSG_MENU_FRIENDSH3EP */
    "FriendSh3ep",
    /* MSG_MENU_ACCOUNTS */
    "Accounts...",
    /* MSG_MENU_NEW_TOOT */
    "New Toot...",
    /* MSG_MENU_ABOUT */
    "About...",
    /* MSG_MENU_QUIT */
    "Quit",

    /* MSG_MENU_VIEW */
    "View",
    /* MSG_VIEW_USER */
    "User (Your posts)",
    /* MSG_VIEW_HOME */
    "Home (Friends & their re-toots)",
    /* MSG_VIEW_LOCAL */
    "Local (Your server & re-toots)",
    /* MSG_VIEW_FEDERATED */
    "Federated (around the world)",
    /* MSG_VIEW_SEARCH */
    "Search",

    "Notifications",
    "Bookmarks",
    "News",

    /* MSG_MENU_SETTINGS */
    "Settings",
    /* MSG_SETTINGS_THEME */
    "Theme Settings",
    /* MSG_SETTINGS_GENERAL */
    "General Settings",
    /* MSG_SETTINGS_FONTSIZEM */
    "Font size -",
    /* MSG_SETTINGS_FONTSIZEP */
    "Font size +",

    /* MSG_THEMEV_TITLE */
    "Font & Theme Settings",
    /* MSG_THEMEV_OPTIONS_GROUP */
    "Options",
    /* MSG_THEMEV_ANTIALIAS */
    "Antialias when possible",
    /* MSG_THEMEV_EMOJIQUALITY */
    "Emoji quality scaling",
    /* MSG_THEMEV_FONTS_GROUP */
    "Fonts",
    /* MSG_THEMEV_PRIMARY */
    "Primary font",
    /* MSG_THEMEV_FALLBACK1 */
    "Fallback font 1",
    /* MSG_THEMEV_FALLBACK2 */
    "Fallback font 2",
    /* MSG_THEMEV_EMOJIFONT */
    "UI emoji font",
    /* MSG_THEMEV_COLOREMOJIFONT */
    "Color emoji font",
    /* MSG_THEMEV_PRESETS_GROUP */
    "Presets",
    /* MSG_THEMEV_PRESET_LOW */
    "Low quality",
    /* MSG_THEMEV_PRESET_HQ */
    "High quality",
    /* MSG_THEMEV_PRESET_MONO */
    "Monospace",
    /* MSG_THEMEV_THEME_GROUP */
    "Theme",
    /* MSG_THEMEV_THEME_NAME */
    "Theme",
    /* MSG_THEMEV_THEME_DEFAULT */
    "Default",

    /* MSG_SETTINGSV_TITLE */
    "General Settings",
    /* MSG_SETTINGSV_PATHS_GROUP */
    "Paths",
    /* MSG_SETTINGSV_CACHE_PATH */
    "Cache directory",
    /* MSG_SETTINGSV_USERDATA_PATH */
    "User data directory",
    /* MSG_SETTINGSV_CACHE_GROUP */
    "Cache",
    /* MSG_SETTINGSV_MAX_CACHE_SIZE */
    "Max cache size (MB)",
    /* MSG_SETTINGSV_FLUSH_CACHE */
    "Flush cache",
    /* MSG_SETTINGSV_SERVER_GROUP */
    "Server",
    /* MSG_SETTINGSV_CHECK_INTERVAL */
    "Check interval (seconds)",
    /* MSG_SETTINGSV_KEEP_BIG_USERICONS */
    "Keep big user icons",
    /* MSG_SETTINGSV_KEEP_BIG_THUMBNAILS */
    "Keep big thumbnails",
    /* MSG_SETTINGSV_THUMBNAILS_GROUP */
    "Thumbnails & icons",
    /* MSG_SETTINGSV_BIGGER_THUMBNAILS */
    "Bigger Thumbnails",
    /* MSG_SETTINGSV_SCALING_QUALITY */
    "Scaling Quality",
    /* MSG_SETTINGSV_SCALEQ_FAST */
    "Fast linear (68020)",
    /* MSG_SETTINGSV_SCALEQ_BILINEAR */
    "Quick Bilinear (68030)",
    /* MSG_SETTINGSV_SCALEQ_TRILINEAR */
    "Full Trilinear (>=68060)",
    /* MSG_SETTINGSV_RGB_DRAW_FUNCTION */
    "RGB Draw function",
    /* MSG_SETTINGSV_RGBDRAW_SCALEPIXELARRAY */
    "ScalePixelArray()",
    /* MSG_SETTINGSV_RGBDRAW_INTERNAL_BILINEAR */
    "Internal Bilinear (>=68060)",
};

/* LocaleBase declared in friendsh3ep.c */
extern struct LocaleBase *LocaleBase;

static struct Catalog *catalog = NULL;

BOOL FS3ELocale_Init(const char *catalogName, ULONG version)
{
    if (LocaleBase && catalogName) {
        catalog = OpenCatalog(NULL, (STRPTR)catalogName,
                              OC_Version, version,
                              OC_BuiltInLanguage, (ULONG)"english",
                              TAG_DONE);
    }
    return TRUE;
}

void FS3ELocale_Close(void)
{
    if (catalog) {
        CloseCatalog(catalog);
        catalog = NULL;
    }
}

const char *FS3ELocale_GetString(ULONG stringID)
{
    if (stringID >= MSG_COUNT) return "???";

    if (LocaleBase && catalog)
        return GetCatalogStr(catalog, stringID, (STRPTR)defaultStrings[stringID]);

    return defaultStrings[stringID];
}
