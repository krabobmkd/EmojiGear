#ifndef BDBPRINTF_H
#define BDBPRINTF_H
/*
 * Buffered and delayed Debug Printf for printing from BOOPSI methods.
 * HandleInput and Render are often executed from a device or interrupt-like
 * context. Using your process Printf/printf there will crash.
 * bdbprintf() works; call flushbdbprint() from the main loop.
 */
#include "compilers.h"

#ifdef USE_DEBUG_BDBPRINT

int bdbprintf(const char *format, ...);
void flushbdbprint(void);
void clearbdbprint(void);
int bdbavailable(void);

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
INLINE int bdbprintf_new(const char *className, void *instance) { (void)className; (void)instance; return 0; }
INLINE int bdbprintf_dispose(const char *className, void *instance) { (void)className; (void)instance; return 0; }
INLINE void bdbprintf_report_leaks(void) {}
INLINE int bdbprintf_makeclass(const char *className, void *classPtr) { (void)className; (void)classPtr; return 0; }
INLINE int bdbprintf_freeclass(const char *className, void *classPtr) { (void)className; (void)classPtr; return 0; }
INLINE void bdbprintf_report_classes(void) {}
#endif

#endif /* BDBPRINTF_H */
