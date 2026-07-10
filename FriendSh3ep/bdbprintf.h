#ifndef BDBPRINTF_H
#define BDBPRINTF_H
/*
 * Buffered and delayed Debug Printf forprinting from BOOPSI methods
 *  HandleInput and Render often are executed from a device or interupt-like context.
 * using your process Printf/printf will crash in that case.
 * bdbprintf() will work. need flushbdbprint() in some main loop in the safe process main().
 */
#include "compilers.h"

#ifdef USE_DEBUG_BDBPRINT

int bdbprintf(const char *format, ...);
void flushbdbprint(void);
void clearbdbprint(void);
int bdbavailable(void);

/* Writes immediately -- no buffering, no waiting for the next
 * flushbdbprint() in the main loop -- through myTask's own pr_COS
 * (the main process's known-good console output), not whatever
 * Output() happens to resolve to for the CALLING task (which may not
 * even be myTask, and may not have a valid DOS process context at
 * all). Only safe to call from a genuinely schedulable task context
 * (a normal process/task, not a hard interrupt) that can tolerate a
 * DOS call blocking -- i.e. NOT from GM_RENDER/GM_HANDLEINPUT's
 * device-like dispatch (see this header's top comment; use plain
 * bdbprintf() there instead). Meant for pinpointing a synchronous
 * crash: a deferred bdbprintf() call right before such a crash is
 * lost with the buffer it never got to flush. */
int bdbprintf_now(const char *format, ...);

int bdbprintf_new(const char *className, void *instance);
int bdbprintf_dispose(const char *className, void *instance);
void bdbprintf_report_leaks(void);

int bdbprintf_makeclass(const char *className, void *classPtr);
int bdbprintf_freeclass(const char *className, void *classPtr);
void bdbprintf_report_classes(void);

#else
INLINE int bdbprintf(const char *format, ...) { (void)format; return 0; }
INLINE void flushbdbprint(void) {}
INLINE void clearbdbprint(void) {}
INLINE int bdbavailable(void)  { return 0; }
INLINE int bdbprintf_now(const char *format, ...) { (void)format; return 0; }
INLINE int bdbprintf_new(const char *className, void *instance) { (void)className; (void)instance; return 0; }
INLINE int bdbprintf_dispose(const char *className, void *instance) { (void)className; (void)instance; return 0; }
INLINE void bdbprintf_report_leaks(void) {}
INLINE int bdbprintf_makeclass(const char *className, void *classPtr) { (void)className; (void)classPtr; return 0; }
INLINE int bdbprintf_freeclass(const char *className, void *classPtr) { (void)className; (void)classPtr; return 0; }
INLINE void bdbprintf_report_classes(void) {}
#endif

#endif /* BDBPRINTF_H */
