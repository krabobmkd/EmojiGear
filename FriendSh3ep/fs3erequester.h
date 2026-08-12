#ifndef FS3EREQUESTER_H
#define FS3EREQUESTER_H

/*
 * fs3erequester.h - requester.class wrapper, replacing intuition.library's
 * EasyRequestArgs() everywhere in FriendSh3ep.
 *
 * Why: EasyRequestArgs always opens at a fixed top-left (0,0) position and
 * has no image support -- requester.class centers over the reference
 * window/screen and can show a question mark/error/warning/info sign (see
 * FS3EReqImage below). requester.class needs ReAction 47+ (RequesterBase --
 * see the libraryTable entry in friendsh3ep.c), which every officially
 * supported target for this app is expected to have; there is deliberately
 * no runtime fallback to EasyRequestArgs -- see friendsh3ep.c's
 * libraryTable comment for why this is a hard requirement, not optional.
 */

#include <exec/types.h>
#include <intuition/intuition.h>

/* Which built-in requester.class image to show -- maps 1:1 to REQIMAGE_*
 * (classes/requester.h), kept as our own enum so callers don't need that
 * header (or requester.class's REQ_* tag names) just to pick one. */
typedef enum FS3EReqImage {
    FS3EREQ_INFO,     /* '!' sign -- plain information, nothing to decide */
    FS3EREQ_QUESTION, /* '?' sign -- the user is being asked to choose */
    FS3EREQ_WARNING,  /* warning sign -- blocked/risky/destructive action */
    FS3EREQ_ERROR     /* error sign -- something failed */
} FS3EReqImage;

/* Drop-in replacement for EasyRequestArgs(win, &es, NULL, NULL) -- same
 * "1, 2, ..., N, 0" left-to-right button numbering (requester.class's
 * RM_OPENREQ autodoc documents the identical convention EasyRequestArgs
 * uses), so every existing choice==N call-site switch/if needs no changes.
 * gadgets is the same "_Ok|_Cancel"-style '|'-separated format both
 * EasyStruct.es_GadgetFormat and REQ_GadgetText use -- text as-is, no
 * reformatting needed at call sites either. win may be NULL (screen-wide
 * requester, same as passing NULL to EasyRequestArgs). */
LONG FS3ERequester_Show(struct Window *win, const char *title, const char *body,
                         const char *gadgets, FS3EReqImage image);

#endif
