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
    /* MSG_TOOT_CONTEXT_QUOTE_FORMAT */
    "Quoting %s's toot",
    /* MSG_TOOT_CONTEXT_MESSAGE_FORMAT */
    "Messaging %s",
    /* MSG_TOOT_ATTACH_MEDIA */
    "Attach Media",
    /* MSG_TOOT_VISIBILITY_PUBLIC */
    "Public",
    /* MSG_TOOT_VISIBILITY_UNLISTED -- API value "unlisted", shown with the
     * official app's newer label */
    "Public (Quiet)",
    /* MSG_TOOT_VISIBILITY_PRIVATE -- API value "private" */
    "Followers",
    /* MSG_TOOT_VISIBILITY_DIRECT -- API value "direct" */
    "Private mention",
    /* MSG_TOOT_VISIBILITY_MEANING_PUBLIC */
    "Visible to everyone, shown in public timelines",
    /* MSG_TOOT_VISIBILITY_MEANING_UNLISTED */
    "Visible to everyone, but hidden from public timelines",
    /* MSG_TOOT_VISIBILITY_MEANING_PRIVATE */
    "Visible only to your followers",
    /* MSG_TOOT_VISIBILITY_MEANING_DIRECT */
    "Visible only to @mentioned accounts - like a PM",
    /* MSG_TOOT_QUOTEPOLICY_PUBLIC -- API value "public" */
    "Everybody",
    /* MSG_TOOT_QUOTEPOLICY_FOLLOWERS -- API value "followers" */
    "Followers only",
    /* MSG_TOOT_QUOTEPOLICY_NOBODY -- API value "nobody" */
    "Me only",
    /* MSG_TOOT_QUOTEPOLICY_MEANING_PUBLIC */
    "Can be quoted by anyone",
    /* MSG_TOOT_QUOTEPOLICY_MEANING_FOLLOWERS */
    "Can be quoted by followers",
    /* MSG_TOOT_QUOTEPOLICY_MEANING_NOBODY */
    "Cannot be quoted",
    /* MSG_TOOT_SEND */
    "Toot",
    /* MSG_TOOT_SEND_MODIFY */
    "Modify",
    /* MSG_TOOT_SEND_REPLY */
    "Reply",
    /* MSG_TOOT_SEND_QUOTE */
    "Quote",
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
    "Refresh",

    /* MSG_MENU_TIMELINE */
    "Timeline",
    /* MSG_TIMELINE_NEXT_TOOT */
    "Next toot",
    /* MSG_TIMELINE_TOP */
    "Move to Top",
    /* MSG_TIMELINE_AUTOSCROLL_PLAY */
    "Autoscroll Play",
    /* MSG_TIMELINE_AUTOSCROLL_STOP */
    "Autoscroll Stop",
    /* MSG_TIMELINE_COPY_TEXT */
    "Copy Toot Text (UTF-8)",

    /* MSG_MENU_USER */
    "User",
    /* MSG_USER_COPY_URL */
    "Copy profile URL",
    /* MSG_USER_FOLLOW */
    "Follow",
    /* MSG_USER_UNFOLLOW */
    "Unfollow",
    /* MSG_USER_MASK */
    "Mask user toots",
    /* MSG_USER_UNMASK */
    "Unmask user toots",
    /* MSG_USER_BLOCK */
    "Block user",
    /* MSG_USER_UNBLOCK */
    "Unblock user",
    /* MSG_USER_BLOCK_SERVER */
    "Block server",
    /* MSG_USER_UNBLOCK_SERVER */
    "Unblock server",

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
    /* MSG_THEMEV_THEME_NONE */
    "-",
    /* MSG_THEMEV_SCAN_THEMES */
    "Scan Themes",

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
    /* MSG_SETTINGSV_SERVERCHECK_GROUP */
    "Server Check",
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
    /* MSG_SETTINGSV_MINIFY_THUMBNAILS */
    "Minify thumbnails source",
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
    /* MSG_SETTINGSV_TOOTPLAYBACK_GROUP */
    "Toot Timeline Playback",
    /* MSG_SETTINGSV_PLAY_TOOT_TIME */
    "Play toot time (seconds)",
    /* MSG_SETTINGSV_ALLOW_NEXT_TOOT_SCROLL */
    "Allow next toot scroll",
    /* MSG_SETTINGSV_URLLINK_GROUP */
    "URL Link",
    /* MSG_SETTINGSV_URLLINK_ACTION */
    "URL Link Action",
    /* MSG_SETTINGSV_URLLINK_ASK */
    "Ask",
    /* MSG_SETTINGSV_URLLINK_OPENURL */
    "Use OpenURL",
    /* MSG_SETTINGSV_URLLINK_CLIPBOARD */
    "Copy to Clipboard",
    /* MSG_SETTINGSV_DIRECT_DL_ARCHIVES */
    "Direct download .zip,.lha",
    /* MSG_SETTINGSV_DOWNLOAD_PATH */
    "Download directory",
    /* MSG_SETTINGSV_TOOT_ACTIONS_DBLCLICK */
    "Toot actions need double click",
    /* MSG_SETTINGSV_TAB_USEREXP */
    "User experience",
    /* MSG_SETTINGSV_TAB_PATHSCACHE */
    "Paths & Cache",

    /* MSG_SEARCHV_WAIT1 */
    "\xF0\x9F\x90\x98 Casting the net across the fediverse...",
    /* MSG_SEARCHV_WAIT2 */
    "Chasing toots through the herd...",
    /* MSG_SEARCHV_WAIT3 */
    "Searching the elephant's memory...",
    /* MSG_SEARCHV_WAIT4 */
    "Combing timelines, one toot at a time...",

    /* MSG_SEARCH_TYPE_WORD */
    "Word",
    /* MSG_SEARCH_TYPE_PEOPLE */
    "People",

    /* MSG_NETWORKV_TITLE */
    "Network",
    /* MSG_NETWORKV_COL_NAME */
    "Name",
    /* MSG_NETWORKV_COL_PROGRESS */
    "Progress",
    /* MSG_NETWORKV_COL_STATUS */
    "Status",
    /* MSG_NETWORKV_STATUS_ON */
    "Active",
    /* MSG_NETWORKV_STATUS_OK */
    "OK",
    /* MSG_NETWORKV_STATUS_ERROR */
    "Error",
    /* MSG_NETWORKV_IDLE */
    "No downloads",
    /* MSG_FIRSTUSE_TITLE */
    "FriendSh3ep - First Use Warning",
    /* MSG_FIRSTUSE_TEXT */
    "\n"
    " FIRST USE WARNING!\n"
    "\n"
    " This app continuously writes tens of megabytes to a cache.\n"
    " This is dangerous if your disk configuration is not 100% reliable.\n"
    "\n"
    " If your Amiga has frequent disk validation (slow boot) or\n"
    " recurrent HD troubles, you're strongly advised to contact\n"
    " a qualified repair technician to service your machine.\n"
    "\n"
    " You may also configure the cache directory on a\n"
    " non-sensitive drive. About 80MB is needed.\n"
    " You may delete the whole .cache dir at any moment.\n"
    "\n"
    "\n"
    "\n",
    /* MSG_FIRSTUSE_GADGETS */
    "Go|Quit",
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
