/*
 * Buffered Debug Printf for Amiga C89
 * Adapted from aukboopsi for Petmate.
 */
#ifdef USE_DEBUG_BDBPRINT
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <proto/dos.h>
#include <proto/exec.h>

#define BDB_BUFFER_SIZE 4096

static char bdb_buffer[BDB_BUFFER_SIZE];
static volatile int bdb_position = 0;

extern struct Task *myTask;

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

    if (myTask) Signal(myTask, SIGBREAKF_CTRL_F);
    return written;
}

void flushbdbprint(void)
{
    if (bdb_position > 0) {
        bdb_buffer[bdb_position] = '\0';
        Printf(bdb_buffer);
        fflush(stdout);
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

/* Instance tracking */
static volatile int instances_created = 0;
static volatile int instances_disposed = 0;

int bdbprintf_new(const char *className, void *instance)
{
    instances_created++;
    return bdbprintf("[NEW] %s instance:%lx (total:%ld)\n",
                     className, (unsigned long)instance, (long)instances_created);
}

int bdbprintf_dispose(const char *className, void *instance)
{
    instances_disposed++;
    return bdbprintf("[DISPOSE] %s instance:%lx (total:%ld)\n",
                     className, (unsigned long)instance, (long)instances_disposed);
}

void bdbprintf_report_leaks(void)
{
    long leaked = instances_created - instances_disposed;
    if (leaked != 0) {
        bdbprintf("\n*** BOOPSI LEAK: created=%ld disposed=%ld leaked=%ld ***\n",
                  (long)instances_created, (long)instances_disposed, leaked);
        flushbdbprint();
    } else if (instances_created > 0) {
        bdbprintf("\nBOOPSI: %ld instances, no leaks.\n", (long)instances_created);
        flushbdbprint();
    }
}

/* Class tracking */
static volatile int classes_made = 0;
static volatile int classes_freed = 0;

int bdbprintf_makeclass(const char *className, void *classPtr)
{
    classes_made++;
    (void)className; (void)classPtr;
    return 0;
}

int bdbprintf_freeclass(const char *className, void *classPtr)
{
    classes_freed++;
    (void)className; (void)classPtr;
    return 0;
}

void bdbprintf_report_classes(void)
{
    long leaked = classes_made - classes_freed;
    if (leaked != 0) {
        bdbprintf("\n*** CLASS LEAK: made=%ld freed=%ld leaked=%ld ***\n",
                  (long)classes_made, (long)classes_freed, leaked);
        flushbdbprint();
    } else if (classes_made > 0) {
        bdbprintf("\nClasses: %ld made/freed, no leaks.\n", (long)classes_made);
        flushbdbprint();
    }
}
#endif
