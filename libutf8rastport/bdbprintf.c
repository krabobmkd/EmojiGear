#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <exec/semaphores.h>
#include <proto/dos.h>
#include <proto/exec.h>

/*
 * Buffered Debug Printf – utf8rastport.library's own copy.
 *
 * bdbprintf()      can be called from any task (input, render, …) --
 *                  and, since this is a shared library's own static data
 *                  rather than an application's private copy, from any
 *                  PROCESS that has the library open, all touching the
 *                  same bdb_buffer/bdb_position. Locked with bdb_sem.
 * flushbdbprint()  is normally driven from one process's main task (see
 *                  FriendSh3ep's URPDC_FlushGlyphCache(NULL) "trick" in
 *                  its event loop), but nothing here assumes that's the
 *                  only caller -- it takes the same lock.
 *
 * bdb_sem is InitSemaphore()'d once via bdbprintf_init(), called from
 * CLibInit() before any process can OpenLibrary() this library -- same
 * one-shot timing as urp_shared_fonts_init()/urp_shared_cluts_init().
 */

#ifdef USE_DEBUG_BDBPRINT

#define BDB_BUFFER_SIZE 4096

static char                   bdb_buffer[BDB_BUFFER_SIZE];
static volatile int           bdb_position = 0;
static struct SignalSemaphore bdb_sem;

void bdbprintf_init(void)
{
    InitSemaphore(&bdb_sem);
}

int bdbprintf(const char *format, ...)
{
    va_list args;
    int remaining;
    int written;

 //   ObtainSemaphore(&bdb_sem);

    remaining = BDB_BUFFER_SIZE - bdb_position - 1;
    if (remaining <= 0) {
    //ReleaseSemaphore(&bdb_sem);
    return 0; }

    va_start(args, format);
    written = vsnprintf(bdb_buffer + bdb_position, remaining + 1, format, args);
    va_end(args);

    if (written > remaining) written = remaining;
    if (written > 0) bdb_position += written;

//    ReleaseSemaphore(&bdb_sem);
    return written;
}

void flushbdbprint(void)
{
    ObtainSemaphore(&bdb_sem);
    if (bdb_position > 0) {
        bdb_buffer[bdb_position] = '\0';
        Printf(bdb_buffer);

        bdb_position = 0;
        bdb_buffer[0] = '\0';
    }
    ReleaseSemaphore(&bdb_sem);
}

void clearbdbprint(void)
{
  //  ObtainSemaphore(&bdb_sem);
    bdb_position = 0;
    bdb_buffer[0] = '\0';
  //  ReleaseSemaphore(&bdb_sem);
}

int bdbavailable(void)
{
    int avail;
  //  ObtainSemaphore(&bdb_sem);
    avail = BDB_BUFFER_SIZE - bdb_position - 1;
  //  ReleaseSemaphore(&bdb_sem);
    return avail;
}

#endif /* USE_DEBUG_BDBPRINT */
