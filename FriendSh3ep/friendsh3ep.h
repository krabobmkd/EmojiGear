#ifndef FRIENDSH3EP_H
#define FRIENDSH3EP_H

/*
 * FriendSh3ep - main application header: struct App and global state.
 * See ARCHITECTURE.md for the overall design.
 */

#include <exec/types.h>
#include <exec/ports.h>
#include <intuition/classusr.h>

#include "fs3eboopsimainwindow.h"
#include "fs3eloginview.h"
#include "fs3etootview.h"

#define FRIENDSH3EP_VERSION "0.1"

/* Application struct: holds every persistent BOOPSI object and IPC handle,
 * in the same spirit as EmojiGear's struct App. */
struct App {
    Object *window_obj;       /* window.class object (persistent) */
    struct MsgPort *app_port; /* needed to keep receiving messages while iconified */

    FS3EMainWindow mainwindow;

    /* main layout (layout.gadget) and its children */
    Object *mainlayout;
    Object *titleLabel;
    Object *loginButton;
    Object *newTootButton;

    /* classic BOOPSI sub-windows, opened/closed independently of the
     * borderless main window (see fs3eloginview.h / fs3etootview.h) */
    FS3ELoginView loginView;
    FS3ETootView  tootView;

    /* fs3enet request port, see network_fs3e/fs3enet.h */
    struct MsgPort *netRequestPort;
};

extern struct App *app;

/* Print pmessage (if non-NULL) and exit(0); runs exitclose() via atexit(). */
void cleanexit(const char *pmessage);

#endif /* FRIENDSH3EP_H */
