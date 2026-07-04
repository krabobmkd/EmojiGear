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
    /* MSG_LOGIN_CODE */
    "Code",
    /* MSG_LOGIN_LOGIN */
    "Login",

    /* MSG_TOOT_TITLE */
    "New toot",
    /* MSG_TOOT_SUBJECT */
    "CW or subject",
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
    /* MSG_TOOT_CHARS_FORMAT */
    "%lu chars",

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
