/*
 * TextEditor gadget – mouse input handling.
 *
 * GM_GOACTIVE / GM_HANDLEINPUT / GM_GOINACTIVE run in the input.device
 * context, which must NOT call FreeType, disk I/O, or any blocking API.
 *
 * These three functions therefore do nothing except record the raw mouse
 * coordinates / qualifiers into inst->pending* fields and send a
 * UTEDN_CursorMoved notification.  All actual hit-testing and cursor/
 * selection updates happen at the start of UniTextEditor_OnRender, which
 * runs in the safe application task context.
 */

#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/keymap.h>
#include <devices/inputevent.h>
#include <clib/alib_protos.h>
#include "unitexteditor_private.h"

/* =========================================================================
 * GM_GOACTIVE  (left button down) – input.device context, no API calls
 * =========================================================================
 */
ULONG UniTextEditor_OnGoActive(Class *cl, Object *o, struct gpInput *msg)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    struct InputEvent *ie   = msg->gpi_IEvent;

    if (!ie || ie->ie_Class != IECLASS_RAWMOUSE) return GMR_NOREUSE;
    if (ie->ie_Code != IECODE_LBUTTON)           return GMR_NOREUSE;

    /* Multi-click detection: proximity + within system double-click timeout */
    {
        WORD dx = (WORD)(msg->gpi_Mouse.X - inst->lastClickX);
        WORD dy = (WORD)(msg->gpi_Mouse.Y - inst->lastClickY);
        if (dx < 0) dx = (WORD)-dx;
        if (dy < 0) dy = (WORD)-dy;
        if (dx <= 4 && dy <= 4 &&
            DoubleClick(inst->lastClickSec, inst->lastClickMicro,
                        (ULONG)ie->ie_TimeStamp.tv_secs,
                        (ULONG)ie->ie_TimeStamp.tv_micro))
        {
            if (inst->pendingClickCount < 3) inst->pendingClickCount++;
        } else {
            inst->pendingClickCount = 1;
        }
        inst->lastClickSec   = (ULONG)ie->ie_TimeStamp.tv_secs;
        inst->lastClickMicro = (ULONG)ie->ie_TimeStamp.tv_micro;
        inst->lastClickX     = (WORD)msg->gpi_Mouse.X;
        inst->lastClickY     = (WORD)msg->gpi_Mouse.Y;
    }

    /* Record click; any previous pending drag is superseded by a new click */
    inst->pendingClick            = TRUE;
    inst->pendingClickX           = msg->gpi_Mouse.X;
    inst->pendingClickY           = msg->gpi_Mouse.Y;
    inst->pendingClickQualifiers  = ie->ie_Qualifier;
    inst->pendingDrag             = FALSE;

    inst->dragging     = TRUE;
    inst->gadgetActive = TRUE;

    uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);

    uted_notify(cl, o, msg->gpi_GInfo, UTED_SetPrivateActivation, TRUE);
    return GMR_MEACTIVE;
}





/* =========================================================================
 * GM_HANDLEINPUT  (mouse moves / button release) – input.device context
 * =========================================================================
 */
ULONG UniTextEditor_OnHandleInput(Class *cl, Object *o, struct gpInput *msg)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    struct InputEvent *ie   = msg->gpi_IEvent;

    if (!ie) return GMR_MEACTIVE;

  if (ie->ie_Class == IECLASS_RAWMOUSE) {

        if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
            /* Button released: record final position, deactivate drag */
            inst->pendingDrag      = TRUE;
            inst->pendingDragType  = UTED_MPEND_RELEASE;
            inst->pendingDragX     = msg->gpi_Mouse.X;
            inst->pendingDragY     = msg->gpi_Mouse.Y;
            inst->dragging         = FALSE;
            inst->gadgetActive     = FALSE;
            uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
            *msg->gpi_Termination  = 0;
            return GMR_VERIFY | GMR_NOREUSE;
        }

        if (inst->dragging) {
            /* Mouse moved: overwrite with latest position (render only needs last) */
            inst->pendingDrag      = TRUE;
            inst->pendingDragType  = UTED_MPEND_DRAG;
            inst->pendingDragX     = msg->gpi_Mouse.X;
            inst->pendingDragY     = msg->gpi_Mouse.Y;
            uted_notify(cl, o, msg->gpi_GInfo, UTEDN_CursorMoved, inst->cursor.line);
        }
    }

    return GMR_MEACTIVE;
}

/* =========================================================================
 * GM_GOINACTIVE – input.device context, no API calls
 * =========================================================================
 */
ULONG UniTextEditor_OnGoInactive(Class *cl, Object *o, struct gpGoInactive *msg)
{
    UniTextEditorData *inst = UTED_DATA(cl, o);
    (void)msg;

// bdbprintf("Inactive %08x\n",(int)o);

    inst->dragging           = FALSE;
    inst->clickAnchorValid   = FALSE;
    /* Discard any pending input that never reached GM_RENDER */
    inst->pendingClick  = FALSE;
    inst->pendingDrag   = FALSE;
    return 0;
}
