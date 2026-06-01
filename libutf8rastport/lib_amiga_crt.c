/*
 * lib_amiga_crt.c — Minimal reentrant C runtime for utf8rastport.library
 *
 * Replaces libnix (-nostdlib). Rules:
 *   - malloc/free  → exec AllocVec/FreeVec  (exec serialises + tracks size)
 *   - setjmp/longjmp → m68k assembler  (needed by FreeType PNG error recovery)
 *   - stderr fprintf/fputc → current task DOS Output()  (reentrant by design)
 *   - fread/fwrite/fflush → stubs  (FreeType supplies its own libpng callbacks)
 *   - no mutable global state except the four __sF pointers (set once at init)
 *
 * Functions provided:
 *   Memory    : malloc, free  (AllocVec/FreeVec; size tracked by exec)
 *   Mem ops   : memcpy, memset, memmove, __bcopz
 *   Strings   : strlen, strcmp, strncmp, strcpy, strncpy, strcat, strrchr, strstr
 *   Stdlib    : qsort, strtol, strtoul, getenv, abort
 *   setjmp    : setjmp, longjmp  (m68k asm, fits inside the 136-byte jmp_buf)
 *   stdio     : FILE, __sF, fprintf, fputc, fread, fwrite, fflush, vsnprintf, snprintf
 *   Time      : gmtime  (returns NULL; libpng skips the tIME chunk gracefully)
 *   libnix stubs: __initmalloc, __exitmalloc, __initstdio, __exitstdio
 *
 *  __udivdi3 / __divdi3  are NOT here; link -lgcc for those.
 */

#include <exec/types.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <stddef.h>
#include <stdarg.h>

/* SysBase and DOSBase are declared by proto/exec.h and proto/dos.h above */

/* ------------------------------------------------------------------ *
 * FILE — our own minimal opaque struct.                               *
 * Callers hold FILE* from __sF[] or from stubs; they never peek       *
 * inside.  Only our own functions interpret the layout.               *
 * ------------------------------------------------------------------ */

#define URPF_STDERR 1   /* route output to current task Output() */

typedef struct { int flags; } FILE;

static FILE          _g_stderr          = { URPF_STDERR };
static FILE         *_g_sF_ptrs[3]      = { NULL, NULL, &_g_stderr };
FILE               **__sF               = _g_sF_ptrs;

/* ------------------------------------------------------------------ *
 * Memory                                                              *
 * ------------------------------------------------------------------ */

void *malloc(size_t size)
{
    if (!size) return NULL;
    return AllocVec(size, MEMF_ANY);
}

void free(void *ptr)
{
    if (ptr) FreeVec(ptr);
}

/* ------------------------------------------------------------------ *
 * Memory operations                                                   *
 * ------------------------------------------------------------------ */

void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) *p++ = (unsigned char)c;
    return s;
}

void *memmove(void *dst, const void *src, size_t n)
{
    char *d = (char *)dst;
    const char *s = (const char *)src;
    if (!n) return dst;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

/* __bcopz: libnix fast block-copy alias  (bcopy convention: src, dst, n) */
void __bcopz(const void *src, void *dst, size_t n)
{
    memmove(dst, src, n);
}

/* ------------------------------------------------------------------ *
 * String functions                                                    *
 * ------------------------------------------------------------------ */

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p) p++;
    return (size_t)(p - s);
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    if (!n) return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

char *strcpy(char *dst, const char *src)
{
    char *d = dst;
    while ((*d++ = *src++));
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n)
{
    char *d = dst;
    while (n && (*d++ = *src++)) n--;
    while (n--) *d++ = '\0';
    return dst;
}

char *strcat(char *dst, const char *src)
{
    char *d = dst;
    while (*d) d++;
    while ((*d++ = *src++));
    return dst;
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    do { if (*s == (char)c) last = s; } while (*s++);
    return (char *)last;
}

char *strstr(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    if (!nl) return (char *)hay;
    for (; *hay; hay++)
        if (*hay == *needle && strncmp(hay, needle, nl) == 0)
            return (char *)hay;
    return NULL;
}

/* ------------------------------------------------------------------ *
 * setjmp / longjmp  — m68k assembler                                  *
 *                                                                     *
 * jmp_buf is int[34] = 136 bytes (machine/setjmp.h for m68k).        *
 * We save 13 longs (52 bytes), all fitting in that buffer:            *
 *   [0]     return PC                                                 *
 *   [1..12] d2-d7/a2-a6/sp   (stored by moveml)                      *
 * SP is at long-index 12, byte offset 48 (used by longjmp).          *
 * ------------------------------------------------------------------ */

asm(
"   .text\n"
"   .even\n"
"   .globl _setjmp\n"
"_setjmp:\n"
"   movel   sp@(4),a0\n"           /* a0 = jmp_buf */
"   movel   sp@,a0@+\n"            /* store return PC; advance a0 past it */
"   moveml  d2-d7/a2-a6/sp,a0@\n" /* store 12 registers */
"   moveql  #0,d0\n"               /* return 0 */
"   rts\n"
);

asm(
"   .text\n"
"   .even\n"
"   .globl _longjmp\n"
"_longjmp:\n"
"   addql   #4,sp\n"               /* discard longjmp's own return address */
"   movel   sp@+,a0\n"             /* a0 = jmp_buf */
"   movel   sp@+,d0\n"             /* d0 = val */
"   jne     .Lurp_ljne\n"
"   moveql  #1,d0\n"               /* val must not be 0 */
".Lurp_ljne:\n"
"   movel   a0@(48),sp\n"          /* restore sp (offset 48 = long 12) */
"   movel   a0@+,sp@\n"            /* place return PC on restored stack */
"   moveml  a0@,d2-d7/a2-a6\n"    /* restore 11 data/address regs */
"   rts\n"
);

/* ------------------------------------------------------------------ *
 * Stdlib utilities                                                    *
 * ------------------------------------------------------------------ */

static int _urp_isspace(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' ||
           c == '\r' || c == '\f' || c == '\v';
}
static int _urp_isdigit(unsigned char c) { return c >= '0' && c <= '9'; }
static int _urp_isalpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

unsigned long strtoul(const char *nptr, char **endptr, int base)
{
    const char *p = nptr;
    unsigned long result = 0;
    int any = 0, neg = 0;

    while (_urp_isspace((unsigned char)*p)) p++;

    if (*p == '-')      { neg = 1; p++; }
    else if (*p == '+') { p++; }

    if ((base == 0 || base == 16) &&
        *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        base = 16; p += 2;
    } else if (base == 0 && *p == '0') {
        base = 8; p++;
    } else if (base == 0) {
        base = 10;
    }

    for (; *p; p++) {
        unsigned char c = (unsigned char)*p;
        int digit;
        if (_urp_isdigit(c))      digit = c - '0';
        else if (_urp_isalpha(c)) digit = (c | 0x20) - 'a' + 10;
        else break;
        if (digit >= base) break;
        result = result * (unsigned)base + (unsigned)digit;
        any = 1;
    }

    if (endptr) *endptr = (char *)(any ? p : nptr);
    return neg ? (unsigned long)(-(long)result) : result;
}

long strtol(const char *nptr, char **endptr, int base)
{
    return (long)strtoul(nptr, endptr, base);
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *))
{
    char *base2 = (char *)base;
    size_t i, a, b, c;
    while (nmemb > 1) {
        a = 0; b = nmemb - 1; c = (a + b) / 2;
        for (;;) {
            while (compar(&base2[size*c], &base2[size*a]) > 0) a++;
            while (compar(&base2[size*c], &base2[size*b]) < 0) b--;
            if (a >= b) break;
            for (i = 0; i < size; i++) {
                char tmp = base2[size*a+i];
                base2[size*a+i] = base2[size*b+i];
                base2[size*b+i] = tmp;
            }
            if (c == a) c = b; else if (c == b) c = a;
            a++; b--;
        }
        b++;
        if (b < nmemb - b) {
            qsort(base2, b, size, compar);
            base2 += size * b; nmemb -= b;
        } else {
            qsort(base2 + size * b, nmemb - b, size, compar);
            nmemb = b;
        }
    }
}

char *getenv(const char *name)
{
    static char buf[256];
    LONG r;
    if (!name || !DOSBase) return NULL;
    r = GetVar((STRPTR)name, buf, (LONG)(sizeof(buf) - 1), 0L);
    if (r < 0) return NULL;
    buf[r] = '\0';
    return buf;
}

void abort(void)
{
    Alert(0x05000001UL);
    for (;;);   /* noreturn */
}

/* gmtime: libpng uses it for optional tIME chunk; NULL skips it */
void *gmtime(const long *t)
{
    (void)t;
    return NULL;
}

/* ------------------------------------------------------------------ *
 * Minimal stdio                                                       *
 * fprintf / fputc route to current task DOS Output() for diagnostics.*
 * fread / fwrite / fflush are stubs (FreeType uses custom callbacks). *
 * ------------------------------------------------------------------ */

static void _urp_putchar(char c)
{
    BPTR out;
    if (!DOSBase) return;
    out = Output();
    if (out) Write(out, &c, 1);
}

static void _urp_puts(const char *s)
{
    BPTR out;
    LONG len = 0;
    const char *p;
    if (!DOSBase || !s) return;
    out = Output();
    if (!out) return;
    for (p = s; *p; p++, len++);
    Write(out, (APTR)s, len);
}

static void _urp_putulong(unsigned long v, int base, int upper)
{
    char buf[20];
    int i = (int)sizeof(buf) - 1;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    buf[i] = '\0';
    if (!v) { buf[--i] = '0'; }
    else { while (v) { buf[--i] = digits[v % (unsigned)base]; v /= (unsigned)base; } }
    _urp_puts(&buf[i]);
}

int fprintf(FILE *fp, const char *fmt, ...)
{
    va_list args;
    const char *p;
    (void)fp;   /* always route to current task Output() */
    va_start(args, fmt);
    for (p = fmt; *p; p++) {
        if (*p != '%') { _urp_putchar(*p); continue; }
        p++;
        int lng = (*p == 'l') ? (p++, 1) : 0;
        switch (*p) {
        case 's':
            _urp_puts(va_arg(args, char *));
            break;
        case 'c':
            _urp_putchar((char)va_arg(args, int));
            break;
        case 'd': case 'i': {
            long v = lng ? va_arg(args, long) : (long)va_arg(args, int);
            if (v < 0) { _urp_putchar('-'); v = -v; }
            _urp_putulong((unsigned long)v, 10, 0);
            break;
        }
        case 'u': {
            unsigned long v = lng ? va_arg(args, unsigned long)
                                  : (unsigned long)va_arg(args, unsigned int);
            _urp_putulong(v, 10, 0);
            break;
        }
        case 'x': {
            unsigned long v = lng ? va_arg(args, unsigned long)
                                  : (unsigned long)va_arg(args, unsigned int);
            _urp_putulong(v, 16, 0);
            break;
        }
        case 'X': {
            unsigned long v = lng ? va_arg(args, unsigned long)
                                  : (unsigned long)va_arg(args, unsigned int);
            _urp_putulong(v, 16, 1);
            break;
        }
        case 'p':
            _urp_putulong((unsigned long)va_arg(args, void *), 16, 0);
            break;
        case '%':
            _urp_putchar('%');
            break;
        default:
            _urp_putchar('%');
            _urp_putchar(*p);
            break;
        }
    }
    va_end(args);
    return 0;
}

/* ------------------------------------------------------------------ *
 * vsnprintf / snprintf                                                *
 *                                                                     *
 * Derived from the libnix __vfprintf_total_size parser (amiga-gcc    *
 * tree, projects/libnix) — FILE/stdio/locking/float stripped out,    *
 * replaced with a bounded buffer writer.                             *
 *                                                                     *
 * Supports: %s %c %d %i %u %x %X %o %p %%                           *
 *           flags: - 0 + (space) #                                   *
 *           width (numeric or *)                                      *
 *           precision (numeric or *)                                  *
 *           length: h  l                                              *
 * ------------------------------------------------------------------ */

/* PUTC writes one char into the bounded buffer (always increments total). */
#define _VSPRINTF_PUTC(c) \
    do { if (_rem > 1) { *_out++ = (char)(c); _rem--; } _total++; } while(0)

int vsnprintf(char *buf, size_t size, const char *fmt, va_list args)
{
    char              *_out   = buf;
    size_t             _rem   = size;
    int                _total = 0;
    const char        *ptr;

    static const char  lohex[] = "0123456789abcdef";
    static const char  uphex[] = "0123456789ABCDEF";

    /* flags */
#define _FL_ALT    1   /* '#' */
#define _FL_ZERO   2   /* '0' */
#define _FL_LALIGN 4   /* '-' */
#define _FL_BLANK  8   /* ' ' */
#define _FL_SIGN  16   /* '+' */

    if (!buf || !size) return 0;

    ptr = fmt;
    while (*ptr) {
        if (*ptr != '%') { _VSPRINTF_PUTC(*ptr++); continue; }
        ptr++;   /* skip '%' */

        /* --- flags --- */
        int    flags = 0;
        int    stop  = 0;
        while (!stop) {
            switch (*ptr) {
            case '#': flags |= _FL_ALT;    ptr++; break;
            case '0': flags |= _FL_ZERO;   ptr++; break;
            case '-': flags |= _FL_LALIGN; ptr++; break;
            case ' ': flags |= _FL_BLANK;  ptr++; break;
            case '+': flags |= _FL_SIGN;   ptr++; break;
            default:  stop = 1;            break;
            }
        }
        if (flags & _FL_LALIGN) flags &= ~_FL_ZERO; /* '-' overrides '0' */

        /* --- width --- */
        int width = 0;
        if (*ptr == '*') { width = va_arg(args, int); ptr++; if (width < 0) { flags |= _FL_LALIGN; width = -width; } }
        else { while (*ptr >= '0' && *ptr <= '9') width = width * 10 + (*ptr++ - '0'); }

        /* --- precision --- */
        int preci = -1;
        if (*ptr == '.') {
            ptr++;
            preci = 0;
            if (*ptr == '*') { preci = va_arg(args, int); if (preci < 0) preci = -1; ptr++; }
            else { while (*ptr >= '0' && *ptr <= '9') preci = preci * 10 + (*ptr++ - '0'); }
        }

        /* --- length modifier --- */
        int lng = 0;
        if      (*ptr == 'l') { lng = 1; ptr++; }
        else if (*ptr == 'h') {          ptr++; }

        char type = *ptr++;

        /* --- format the value into a local buffer --- */
        char         body[32];
        const char  *bp      = body;
        int          blen    = 0;
        char         sign    = 0;
        char         pre[3];
        int          prelen  = 0;
        pre[0] = pre[1] = pre[2] = 0;

        switch (type) {

        case 's': {
            const char *s = va_arg(args, const char *);
            int l;
            if (!s) s = "(null)";
            for (l = 0; s[l]; l++);
            bp   = s;
            blen = (preci >= 0 && preci < l) ? preci : l;
            break;
        }
        case 'c':
            body[0] = (char)va_arg(args, int);
            blen = 1;
            break;

        case 'd': case 'i': {
            long        v = lng ? va_arg(args, long) : (long)va_arg(args, int);
            unsigned long uv;
            int i;
            if      (v < 0)             { sign = '-'; uv = (unsigned long)(-v); }
            else if (flags & _FL_SIGN)  { sign = '+'; uv = (unsigned long)v; }
            else if (flags & _FL_BLANK) { sign = ' '; uv = (unsigned long)v; }
            else                        {              uv = (unsigned long)v; }
            i = (int)sizeof(body) - 1; body[i] = '\0';
            if (!uv) { body[--i] = '0'; }
            else { while (uv) { body[--i] = (char)('0' + (int)(uv % 10)); uv /= 10; } }
            bp = &body[i]; blen = (int)sizeof(body) - 1 - i;
            break;
        }
        case 'u': {
            unsigned long v = lng ? va_arg(args, unsigned long)
                                  : (unsigned long)va_arg(args, unsigned int);
            int i = (int)sizeof(body) - 1; body[i] = '\0';
            if (!v) { body[--i] = '0'; }
            else { while (v) { body[--i] = (char)('0' + (int)(v % 10)); v /= 10; } }
            bp = &body[i]; blen = (int)sizeof(body) - 1 - i;
            break;
        }
        case 'x': case 'X': {
            const char *dig = (type == 'X') ? uphex : lohex;
            unsigned long v = lng ? va_arg(args, unsigned long)
                                  : (unsigned long)va_arg(args, unsigned int);
            int i;
            if ((flags & _FL_ALT) && v) { pre[0] = '0'; pre[1] = type; prelen = 2; }
            i = (int)sizeof(body) - 1; body[i] = '\0';
            if (!v) { body[--i] = '0'; }
            else { while (v) { body[--i] = dig[v & 0xf]; v >>= 4; } }
            bp = &body[i]; blen = (int)sizeof(body) - 1 - i;
            break;
        }
        case 'o': {
            unsigned long v = lng ? va_arg(args, unsigned long)
                                  : (unsigned long)va_arg(args, unsigned int);
            int i;
            if ((flags & _FL_ALT) && v) { pre[0] = '0'; prelen = 1; }
            i = (int)sizeof(body) - 1; body[i] = '\0';
            if (!v) { body[--i] = '0'; }
            else { while (v) { body[--i] = (char)('0' + (int)(v & 7)); v >>= 3; } }
            bp = &body[i]; blen = (int)sizeof(body) - 1 - i;
            break;
        }
        case 'p': {
            unsigned long v = (unsigned long)va_arg(args, void *);
            int i;
            pre[0] = '0'; pre[1] = 'x'; prelen = 2;
            i = (int)sizeof(body) - 1; body[i] = '\0';
            if (!v) { body[--i] = '0'; }
            else { while (v) { body[--i] = lohex[v & 0xf]; v >>= 4; } }
            bp = &body[i]; blen = (int)sizeof(body) - 1 - i;
            break;
        }
        case '%':
            body[0] = '%'; blen = 1;
            break;
        default:
            body[0] = '%'; body[1] = type; blen = type ? 2 : 1;
            break;
        }

        /* --- emit with padding --- */
        {
            int signlen  = sign   ? 1 : 0;
            int content  = signlen + prelen + blen;
            int pad      = (width > content) ? width - content : 0;
            int pi, si;

            if (!(flags & _FL_LALIGN)) {
                if (flags & _FL_ZERO) {
                    /* zero-pad: sign + prefix first, then zeros */
                    if (sign)  _VSPRINTF_PUTC(sign);
                    for (pi = 0; pi < prelen; pi++) _VSPRINTF_PUTC(pre[pi]);
                    for (pi = 0; pi < pad;    pi++) _VSPRINTF_PUTC('0');
                } else {
                    /* space-pad left */
                    for (pi = 0; pi < pad;    pi++) _VSPRINTF_PUTC(' ');
                    if (sign)  _VSPRINTF_PUTC(sign);
                    for (pi = 0; pi < prelen; pi++) _VSPRINTF_PUTC(pre[pi]);
                }
            } else {
                if (sign)  _VSPRINTF_PUTC(sign);
                for (pi = 0; pi < prelen; pi++) _VSPRINTF_PUTC(pre[pi]);
            }
            for (si = 0; si < blen; si++) _VSPRINTF_PUTC(bp[si]);
            if (flags & _FL_LALIGN)
                for (pi = 0; pi < pad; pi++) _VSPRINTF_PUTC(' ');
        }
    }

    *_out = '\0';
    return _total;

#undef _FL_ALT
#undef _FL_ZERO
#undef _FL_LALIGN
#undef _FL_BLANK
#undef _FL_SIGN
}
#undef _VSPRINTF_PUTC

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list args;
    int r;
    va_start(args, fmt);
    r = vsnprintf(buf, size, fmt, args);
    va_end(args);
    return r;
}

int fputc(int c, FILE *fp)
{
    (void)fp;
    _urp_putchar((char)c);
    return c;
}

size_t fread(void *buf, size_t size, size_t count, FILE *fp)
{
    (void)buf; (void)size; (void)count; (void)fp;
    return 0;   /* stub — FreeType provides its own libpng read callback */
}

size_t fwrite(const void *buf, size_t size, size_t count, FILE *fp)
{
    (void)buf; (void)size; (void)fp;
    return count;   /* stub — pretend success */
}

int fflush(FILE *fp)
{
    (void)fp;
    return 0;
}

