#ifndef EGGADGETID_H
#define EGGADGETID_H

/**
 * Gadget ID definitions for EmojiGear Unicode Text Editor
 *
 * These IDs identify specific gadget instances for event routing
 * through the BoopsiDelay message queue.
 */

/* the window border scroll gadgets managed at gadtools level  */
#define GID_UARROW      2
#define GID_DARROW      3
#define GID_YPROP       4
#define GID_LARROW      5
#define GID_RARROW      6
#define GID_XPROP       7

/* our gadget instance for the UniTextEditor */
#define ID_TEXTEDITOR           1337

/* Tab bar */
#define GID_TABBAR              1350

/* Status bar UniButton (emoji shortcut) */
#define GID_EMOJI_BUTTON        1347

/* Emoji table window */
#define GID_EMOJIBOX_CHOOSER         1360
#define GID_EMOJIBOX_GRID            1361
#define GID_EMOJIBOX_ANSI_BOLD       1362
#define GID_EMOJIBOX_ANSI_ITALIC     1363
#define GID_EMOJIBOX_ANSI_UNDERLINE  1364
#define GID_EMOJIBOX_ANSI_INVERSE  1365

#define GID_EMOJIBOX_ANSI_BLUE       1366
#define GID_EMOJIBOX_ANSI_RED        1367
#define GID_EMOJIBOX_ANSI_WHITE      1368
#define GID_EMOJIBOX_ANSI_BLACK      1369
#define GID_EMOJIBOX_ANSI_NORMAL     1370
#define GID_EMOJIBOX_ANSI_GREEN      1371
#define GID_EMOJIBOX_ANSI_YELLOW     1372

/* Search & Replace box gadgets */
#define ID_SEARCH_EDITOR        1338
#define ID_SEARCH_ERASE         1339
#define ID_REPLACE_EDITOR       1340
#define ID_REPLACE_ERASE        1341
#define ID_SEARCH_FIND_NEXT     1342
#define ID_SEARCH_FIND_PREV     1343
#define ID_SEARCH_REPLACE       1344
#define ID_SEARCH_REPLACE_ALL   1345
#define ID_SEARCH_CASE_SENS     1346


#endif /* GADGETID_H */
