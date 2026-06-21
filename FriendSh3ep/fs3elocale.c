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
