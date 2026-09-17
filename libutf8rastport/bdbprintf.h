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

/* One-time InitSemaphore() for bdb_sem below -- call once from a place
 * guaranteed to run before any other task can reach this library's
 * bdbprintf() (CLibInit(), same timing as urp_shared_fonts_init()).
 * bdb_buffer/bdb_position are this library's own static globals, which
 * (unlike an application's private bdbprintf.c copy) means every process
 * that has utf8rastport.library open shares the SAME buffer -- callable
 * from any task per the comment below, so it needs real locking, not
 * just "flush from the main task" discipline. */
void bdbprintf_init(void);

#else
INLINE int bdbprintf(const char *format, ...) { (void)format; return 0; }
INLINE void flushbdbprint(void) {}
INLINE void clearbdbprint(void) {}
INLINE int bdbavailable(void)  { return 0; }
INLINE void bdbprintf_init(void) {}

#endif

void slowDownAndTag(const char *tagname);

#endif /* BDBPRINTF_H */
