#ifndef FS3EGADGETID_H
#define FS3EGADGETID_H

/*
 * Gadget ID definitions for FriendSh3ep.
 *
 * These IDs identify specific gadget instances for event routing through
 * the BoopsiDelay message queue (see fs3eboopsimessage.c).
 */

/* Login window (fs3eloginview.c) */
#define GID_LOGIN_SERVER_EDITOR  10
#define GID_LOGIN_USER_EDITOR    11
#define GID_LOGIN_CODE_EDITOR    12
#define GID_LOGIN_LOGIN_BUTTON   13

/* New toot window (fs3etootview.c) */
#define GID_TOOT_SUBJECT_EDITOR  20
#define GID_TOOT_BODY_EDITOR     21
#define GID_TOOT_VISIBILITY      22
#define GID_TOOT_EMOJI1_BUTTON   23
#define GID_TOOT_EMOJI2_BUTTON   24
#define GID_TOOT_SEND_BUTTON     25

/* Title bar (Part A, TitleBarLayout) */
#define GID_TITLEBAR_CLOSE       30
#define GID_TITLEBAR_ICONIFY     31
#define GID_TITLEBAR_ALTPOS      32
#define GID_TITLEBAR_DEPTH       33

/* Navigation bar (Part B, NavBarLayout) */
#define GID_NAV_HOME             40
#define GID_NAV_NOTIFICATIONS    41
#define GID_NAV_LOCAL            42
#define GID_NAV_FEDERATED        43
#define GID_NAV_NEWTOOT          44
#define GID_NAV_SEARCH           45
#define GID_NAV_SETTINGS         46
#define GID_NAV_ACCOUNTS         47

#endif /* FS3EGADGETID_H */
