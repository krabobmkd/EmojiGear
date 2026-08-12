#include "fs3erequester.h"

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/alib.h>
#include <proto/requester.h>
#include <classes/requester.h>

static ULONG FS3ERequester_ImageTag(FS3EReqImage image)
{
    switch (image) {
        case FS3EREQ_QUESTION: return REQIMAGE_QUESTION;
        case FS3EREQ_WARNING:  return REQIMAGE_WARNING;
        case FS3EREQ_ERROR:    return REQIMAGE_ERROR;
        default:                return REQIMAGE_INFO;
    }
}

LONG FS3ERequester_Show(struct Window *win, const char *title, const char *body,
                         const char *gadgets, FS3EReqImage image)
{
    Object *reqObj;
    LONG    result = 0;

    reqObj = NewObject(REQUESTER_GetClass(), NULL,
        REQ_Type,       REQTYPE_INFO,
        REQ_TitleText,  (ULONG)title,
        REQ_BodyText,   (ULONG)body,
        REQ_GadgetText, (ULONG)gadgets,
        REQ_Image,      FS3ERequester_ImageTag(image),
        TAG_END);

    if (reqObj) {
        result = (LONG)DoMethod(reqObj, RM_OPENREQ, NULL, win, NULL);
        DisposeObject(reqObj);
    }

    return result;
}
