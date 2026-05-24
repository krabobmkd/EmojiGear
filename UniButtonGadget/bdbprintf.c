#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/exec.h>

/*
 * Buffered Debug Printf – private copy for UniTextEditorGadget.
 *
 * bdbprintf()      can be called from any task (input, render, …).
 * flushbdbprint()  must be called from the application main task,
 *                  e.g. via SetGadgetAttrs(…, UTED_FlushDebugOutput, TRUE, …).
 *
 * No Signal() mechanism needed: the main task drives flushing explicitly
 * by setting the UTED_FlushDebugOutput attribute in its event loop.
 */

#ifdef USE_DEBUG_BDBPRINT

#define BDB_BUFFER_SIZE 4096

static char         bdb_buffer[BDB_BUFFER_SIZE];
static volatile int bdb_position = 0;

int bdbprintf(const char *format, ...)
{
    va_list args;
    int remaining;
    int written;

    remaining = BDB_BUFFER_SIZE - bdb_position - 1;
    if (remaining <= 0) return 0;

    va_start(args, format);
    written = vsnprintf(bdb_buffer + bdb_position, remaining + 1, format, args);
    va_end(args);

    if (written > remaining) written = remaining;
    if (written > 0) bdb_position += written;
    return written;
}

void flushbdbprint(void)
{
    if (bdb_position > 0) {
        bdb_buffer[bdb_position] = '\0';
        Printf(bdb_buffer);

        bdb_position = 0;
        bdb_buffer[0] = '\0';
    }
}

void clearbdbprint(void)
{
    bdb_position = 0;
    bdb_buffer[0] = '\0';
}

int bdbavailable(void)
{
    return BDB_BUFFER_SIZE - bdb_position - 1;
}

void exit(void)
{

}

#endif /* USE_DEBUG_BDBPRINT */
