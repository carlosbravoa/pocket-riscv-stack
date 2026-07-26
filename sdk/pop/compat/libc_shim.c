/*
 * libc_shim.c — minimal libc pieces for the Wolf4SDL riscv-stack port (copied from sdk/tyrian).
 *
 * LiteX links picolibc-MINIMAL: only ~28 symbols (printf/vfprintf to the
 * UART, str(n)cmp/cpy, strchr, strtoul, mem*), and gamelib.c adds
 * malloc/free/calloc/realloc/memcmp. Everything else the game links against
 * is implemented here, smallest-correct-thing style:
 *
 *   formatted:  vsnprintf/snprintf/sprintf/vsprintf (int, %s/%c/%x/%f...),
 *               vsscanf/sscanf (%d %i %u %x %c %n — the exact set used)
 *   stdlib:     exit/abort (-> sys_exit), abs/labs, atoi/atol, strtol,
 *               rand/srand (LCG)
 *   string:     strcat/strncat/strrchr
 *   ctype:      toupper/tolower
 *   time:       time() (=0), localtime() (static zeroed tm -> never Dec 25)
 *
 * GPL-2.0-or-later (port glue; see compat/SDL.h).
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>

void sys_exit(void);                    /* HAL: reboot to the game picker */

/* ------------------------------------------------------------ stdlib --- */

void exit(int status)
{
	(void)status;
	sys_exit();
	for (;;)                            /* not reached */
		;
}

void abort(void)
{
	exit(1);
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

/* rand/srand now live in gamelib (SDK-wide). */

long strtol(const char *s, char **end, int base)
{
	while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
		s++;
	int neg = 0;
	if (*s == '+' || *s == '-')
		neg = (*s++ == '-');
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
		s += 2;
		base = 16;
	} else if (base == 0) {
		base = (s[0] == '0') ? 8 : 10;
	}
	long v = 0;
	const char *start = s;
	for (;; s++) {
		int d;
		if (*s >= '0' && *s <= '9')       d = *s - '0';
		else if (*s >= 'a' && *s <= 'z')  d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')  d = *s - 'A' + 10;
		else break;
		if (d >= base)
			break;
		v = v * base + d;
	}
	if (end)
		*end = (char *)(s == start ? start : s);
	return neg ? -v : v;
}

int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }

char *strerror(int errnum)
{
	(void)errnum;
	return (char *)"error";             /* only feeds warning printf()s */
}

/* fileno/fsync: rvfs_fileno (rvfile.c) handles fileno via the shadow header. */
int fsync(int fd) { (void)fd; return 0; }

/* ------------------------------------------------------------ string --- */

size_t strlen(const char *);            /* picolibc */
char *strcpy(char *, const char *);

char *strcat(char *d, const char *s)
{
	strcpy(d + strlen(d), s);
	return d;
}

char *strncat(char *d, const char *s, size_t n)
{
	char *p = d + strlen(d);
	size_t i = 0;
	for (; i < n && s[i]; i++)
		p[i] = s[i];
	p[i] = 0;
	return d;
}

char *strrchr(const char *s, int c)
{
	const char *last = 0;
	do {
		if (*s == (char)c)
			last = s;
	} while (*s++);
	return (char *)last;
}

/* ------------------------------------------------------------- ctype --- */

int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }

/* -------------------------------------------------------------- time --- */

time_t time(time_t *t)
{
	if (t)
		*t = 0;
	return 0;                           /* no RTC; epoch = not christmas */
}

struct tm *localtime(const time_t *t)
{
	(void)t;
	static struct tm tm;                /* all zeros: Jan 1 1970 00:00 */
	return &tm;
}

/* ------------------------------------------------- formatted output --- */
/* Small printf core: flags - 0, width (and *), precision, lengths hh h l
 * ll z, conversions d i u x X o p c s f n %.  Enough for Tyrian's usage
 * (%s %d %c %lu %x %u %hhu %i %% %p %n %2.3f %-5d and zero-pad widths). */

typedef struct {
	char  *dst;
	size_t cap;                         /* including NUL */
	size_t len;                         /* would-be length (snprintf return) */
} outbuf_t;

static void ob_putc(outbuf_t *ob, char c)
{
	if (ob->len + 1 < ob->cap)
		ob->dst[ob->len] = c;
	ob->len++;
}

static void ob_pad(outbuf_t *ob, char c, int n)
{
	while (n-- > 0)
		ob_putc(ob, c);
}

static void ob_str(outbuf_t *ob, const char *s, int max, int width, int left, char padc)
{
	int slen = 0;
	while (s[slen] && (max < 0 || slen < max))
		slen++;
	if (!left)
		ob_pad(ob, padc, width - slen);
	for (int i = 0; i < slen; i++)
		ob_putc(ob, s[i]);
	if (left)
		ob_pad(ob, ' ', width - slen);
}

static void ob_num(outbuf_t *ob, unsigned long long v, int base, int upper,
                   int neg, int width, int prec, int left, int zero)
{
	char tmp[24];
	int n = 0;
	const char *digs = upper ? "0123456789ABCDEF" : "0123456789abcdef";
	do {
		tmp[n++] = digs[v % (unsigned)base];
		v /= (unsigned)base;
	} while (v && n < (int)sizeof(tmp));
	while (n < prec && n < (int)sizeof(tmp))
		tmp[n++] = '0';
	int total = n + (neg ? 1 : 0);
	if (!left && !zero)
		ob_pad(ob, ' ', width - total);
	if (neg)
		ob_putc(ob, '-');
	if (!left && zero)
		ob_pad(ob, '0', width - total);
	while (n > 0)
		ob_putc(ob, tmp[--n]);
	if (left)
		ob_pad(ob, ' ', width - total);
}

int vsnprintf(char *dst, size_t cap, const char *fmt, va_list ap)
{
	outbuf_t ob = { dst, cap, 0 };
	const char *fmt0 = fmt;         /* for the bad-%s diagnostic below */

	for (; *fmt; fmt++) {
		if (*fmt != '%') {
			ob_putc(&ob, *fmt);
			continue;
		}
		fmt++;
		if (*fmt == '%') {
			ob_putc(&ob, '%');
			continue;
		}
		int left = 0, zero = 0;
		for (;; fmt++) {
			if (*fmt == '-')      left = 1;
			else if (*fmt == '0') zero = 1;
			else if (*fmt == '+' || *fmt == ' ' || *fmt == '#') ;
			else break;
		}
		int width = 0;
		if (*fmt == '*') {
			width = va_arg(ap, int);
			if (width < 0) { left = 1; width = -width; }
			fmt++;
		} else {
			while (*fmt >= '0' && *fmt <= '9')
				width = width * 10 + (*fmt++ - '0');
		}
		int prec = -1;
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			if (*fmt == '*') {
				prec = va_arg(ap, int);
				fmt++;
			} else {
				while (*fmt >= '0' && *fmt <= '9')
					prec = prec * 10 + (*fmt++ - '0');
			}
		}
		int lmod = 0;                   /* -2 hh, -1 h, 1 l, 2 ll/z */
		while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z') {
			if (*fmt == 'h')      lmod--;
			else if (*fmt == 'l') lmod++;
			else                  lmod = 2;
			fmt++;
		}
		switch (*fmt) {
		case 'd': case 'i': {
			long long v = (lmod >= 2) ? va_arg(ap, long long)
			           : (lmod == 1) ? va_arg(ap, long)
			           : va_arg(ap, int);
			if (lmod == -1) v = (short)v;
			if (lmod <= -2) v = (signed char)v;
			int neg = v < 0;
			ob_num(&ob, neg ? (unsigned long long)-v : (unsigned long long)v,
			       10, 0, neg, width, prec, left, zero);
			break;
		}
		case 'u': case 'x': case 'X': case 'o': {
			unsigned long long v = (lmod >= 2) ? va_arg(ap, unsigned long long)
			                    : (lmod == 1) ? va_arg(ap, unsigned long)
			                    : va_arg(ap, unsigned int);
			if (lmod == -1) v = (unsigned short)v;
			if (lmod <= -2) v = (unsigned char)v;
			int base = (*fmt == 'u') ? 10 : (*fmt == 'o') ? 8 : 16;
			ob_num(&ob, v, base, *fmt == 'X', 0, width, prec, left, zero);
			break;
		}
		case 'p': {
			void *p = va_arg(ap, void *);
			ob_str(&ob, "0x", -1, 0, 0, ' ');
			ob_num(&ob, (uintptr_t)p, 16, 0, 0, 0, 8, 0, 0);
			break;
		}
		case 'c': {
			char c = (char)va_arg(ap, int);
			if (!left)
				ob_pad(&ob, ' ', width - 1);
			ob_putc(&ob, c);
			if (left)
				ob_pad(&ob, ' ', width - 1);
			break;
		}
		case 's': {
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
#ifndef RVSTACK_PC
			/* Guard against a garbage %s pointer (would fault on rv32). Valid
			 * string RAM: ROM<0x10000, SRAM 0x1000_0000, DRAM 0x4000_0000+.
			 * On a bad ptr, emit the format-string address (maps to rodata ->
			 * names the call site) + the bad ptr, then substitute so boot
			 * continues. Should never fire once the port is clean. */
			else {
				uintptr_t p = (uintptr_t)s;
				if (!(p < 0x10000u ||
				      (p >= 0x10000000u && p < 0x10010000u) ||
				      (p >= 0x40000000u && p < 0x44000000u))) {
					extern void sys_diag(unsigned);
					sys_diag(0xF1170000u);              /* bad-%s marker */
					sys_diag((unsigned)(uintptr_t)fmt0);/* format str addr */
					sys_diag((unsigned)p);              /* the bad pointer */
					s = "(bad)";
				}
			}
#endif
			ob_str(&ob, s, prec, width, left, zero ? '0' : ' ');
			break;
		}
		case 'f': case 'F': case 'g': case 'G': {
			double d = va_arg(ap, double);
			int fneg = d < 0;
			if (fneg)
				d = -d;
			if (prec < 0)
				prec = 6;
			/* signed 64-bit: compiler_rt here has __fixdfdi/__floatdidf
			 * but no unsigned-64 variants */
			long long ip = (long long)d;
			double frac = d - (double)ip;
			char fb[16];
			int fn = 0;
			for (int i = 0; i < prec && fn < (int)sizeof(fb) - 1; i++) {
				frac *= 10.0;
				int dig = (int)frac;
				fb[fn++] = (char)('0' + dig);
				frac -= dig;
			}
			fb[fn] = 0;
			ob_num(&ob, ip, 10, 0, fneg, 0, -1, 0, 0);
			if (prec > 0) {
				ob_putc(&ob, '.');
				ob_str(&ob, fb, -1, 0, 1, ' ');
			}
			break;
		}
		case 'n':
			*va_arg(ap, int *) = (int)ob.len;
			break;
		default:
			ob_putc(&ob, *fmt);
			break;
		}
	}

	if (cap)
		dst[(ob.len < cap) ? ob.len : cap - 1] = 0;
	return (int)ob.len;
}

int snprintf(char *dst, size_t cap, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(dst, cap, fmt, ap);
	va_end(ap);
	return n;
}

int vsprintf(char *dst, const char *fmt, va_list ap)
{
	return vsnprintf(dst, (size_t)1 << 30, fmt, ap);
}

int sprintf(char *dst, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsprintf(dst, fmt, ap);
	va_end(ap);
	return n;
}

/* -------------------------------------------------- formatted input --- */
/* Mini sscanf: whitespace skipping, literal matching, %d %i %u %x %c %n
 * (with optional width) — exactly what joystick.c / params.c /
 * config_file.c use. Returns the number of conversions stored. */

static int scan_int(const char **sp, long *out, int base)
{
	char *end;
	long v = strtol(*sp, &end, base);
	if (end == *sp)
		return 0;
	*out = v;
	*sp = end;
	return 1;
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
	const char *start = s;
	int stored = 0;

	for (; *fmt; fmt++) {
		if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n') {
			while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
				s++;
			continue;
		}
		if (*fmt != '%') {
			if (*s != *fmt)
				return stored;
			s++;
			continue;
		}
		fmt++;
		if (*fmt == '%') {
			if (*s != '%')
				return stored;
			s++;
			continue;
		}
		/* width (parsed, only honored for %c) */
		int width = 0;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		while (*fmt == 'h' || *fmt == 'l')
			fmt++;
		long v;
		switch (*fmt) {
		case 'd':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
			if (!scan_int(&s, &v, 10)) return stored;
			*va_arg(ap, int *) = (int)v;
			stored++;
			break;
		case 'i':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
			if (!scan_int(&s, &v, 0)) return stored;
			*va_arg(ap, int *) = (int)v;
			stored++;
			break;
		case 'u':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
			if (!scan_int(&s, &v, 10) || v < 0) return stored;
			*va_arg(ap, unsigned *) = (unsigned)v;
			stored++;
			break;
		case 'x':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
			if (!scan_int(&s, &v, 16)) return stored;
			*va_arg(ap, unsigned *) = (unsigned)v;
			stored++;
			break;
		case 'c': {
			if (width == 0)
				width = 1;
			char *dst = va_arg(ap, char *);
			for (int i = 0; i < width; i++) {
				if (!*s)
					return stored;
				dst[i] = *s++;
			}
			stored++;
			break;
		}
		case 'n':
			*va_arg(ap, int *) = (int)(s - start);
			break;
		default:
			return stored;
		}
	}
	return stored;
}

int sscanf(const char *s, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vsscanf(s, fmt, ap);
	va_end(ap);
	return n;
}

// picolibc-minimal lacks strdup (US_Print)
void *malloc(size_t);
void *memcpy(void *, const void *, size_t);
char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p)
		memcpy(p, s, n);
	return p;
}

/* ── SDLPoP additions (not in the Wolf3D shim) ────────────────────────────── */
#include <ctype.h>
int strcasecmp(const char *a, const char *b) {
	for (; *a && *b; a++, b++) { int ca=tolower((unsigned char)*a), cb=tolower((unsigned char)*b); if (ca!=cb) return ca-cb; }
	return (unsigned char)*a - (unsigned char)*b;
}
int strncasecmp(const char *a, const char *b, size_t n) {
	for (; n && *a && *b; a++, b++, n--) { int ca=tolower((unsigned char)*a), cb=tolower((unsigned char)*b); if (ca!=cb) return ca-cb; }
	return n ? (unsigned char)*a - (unsigned char)*b : 0;
}

#ifndef RVSTACK_PC
#include "dirent.h"
DIR *opendir(const char *name) { (void)name; return 0; }
struct dirent *readdir(DIR *d) { (void)d; return 0; }
int  closedir(DIR *d) { (void)d; return 0; }
#endif

/* ── more picolibc-minimal gaps for SDLPoP ────────────────────────────────── */
#include <stdio.h>

void perror(const char *s) { if (s && *s) printf("%s: error\n", s); else printf("error\n"); }
char *getenv(const char *n) { (void)n; return 0; }            /* no env on console */
int   mkdir(const char *p, unsigned m) { (void)p; (void)m; return 0; }  /* saves via HAL */
int   chdir(const char *p) { (void)p; return 0; }
double difftime(time_t a, time_t b) { return (double)(a - b); }
long long strtoll(const char *s, char **e, int b);
intmax_t strtoimax(const char *s, char **e, int b) { return strtol(s, e, b); }
/* fscanf: the only files read with it (SDLPoP.ini, names.txt) are declined by
 * rvfile (fopen->NULL), so this never runs on a real handle — link stub. */
int fscanf(FILE *f, const char *fmt, ...) { (void)f; (void)fmt; return -1; }
/* stat: real (non-pak) stat has no filesystem on console — report "not found";
 * open_dat's dataset check uses rvfs_stat() instead (RVSTACK edit). */
struct stat;
int stat(const char *p, struct stat *st) { (void)p; (void)st; return -1; }

/* qsort — simple insertion sort (PoP sorts tiny lists) */
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void*, const void*)) {
	char *a = (char*)base, tmp[256];
	if (sz > sizeof tmp) return;
	for (size_t i = 1; i < n; i++)
		for (size_t j = i; j > 0 && cmp(a+(j-1)*sz, a+j*sz) > 0; j--) {
			memcpy(tmp, a+(j-1)*sz, sz);
			memcpy(a+(j-1)*sz, a+j*sz, sz);
			memcpy(a+j*sz, tmp, sz);
		}
}

/* rv32 setjmp/longjmp (ilp32 soft-float: only integer callee-saved regs). */
#ifndef RVSTACK_PC
__asm__(
"  .text\n"
"  .global setjmp\n"
"setjmp:\n"
"  sw ra, 0(a0)\n  sw sp, 4(a0)\n  sw s0, 8(a0)\n  sw s1, 12(a0)\n"
"  sw s2,16(a0)\n  sw s3,20(a0)\n  sw s4,24(a0)\n  sw s5,28(a0)\n"
"  sw s6,32(a0)\n  sw s7,36(a0)\n  sw s8,40(a0)\n  sw s9,44(a0)\n"
"  sw s10,48(a0)\n sw s11,52(a0)\n li a0, 0\n  ret\n"
"  .global longjmp\n"
"longjmp:\n"
"  lw ra, 0(a0)\n  lw sp, 4(a0)\n  lw s0, 8(a0)\n  lw s1, 12(a0)\n"
"  lw s2,16(a0)\n  lw s3,20(a0)\n  lw s4,24(a0)\n  lw s5,28(a0)\n"
"  lw s6,32(a0)\n  lw s7,36(a0)\n  lw s8,40(a0)\n  lw s9,44(a0)\n"
"  lw s10,48(a0)\n lw s11,52(a0)\n mv a0, a1\n  bnez a0, 1f\n  li a0, 1\n"
"1: ret\n"
);
#endif

/* ── 64-bit int <-> float helpers compiler_rt lacks (PoP's double/atan2 use) ──
 * Built from the 32-bit conversions compiler_rt DOES provide. */
/* __bswapsi2 — DO NOT implement this as `return __builtin_bswap32(x);`.
 *
 * rv32im has no byte-swap instruction (that's Zbb), so GCC lowers
 * __builtin_bswap32() to a LIBCALL — to __bswapsi2 — i.e. straight back into
 * this function. The result is unconditional infinite recursion:
 *
 *     __bswapsi2:  addi sp,sp,-16 ; sw ra,12(sp) ; jal __bswapsi2
 *
 * which walks the stack downward writing one word every 16 bytes until sp
 * leaves valid memory. That scribble is visible as thin vertical lines at a
 * 16-PIXEL pitch once it crosses the framebuffer, and the eventual fault is
 * unrecoverable: rvstack_trap's own first stack store re-faults on the ruined
 * sp, so it never reaches its sys_diag/red-bar report. Symptom on hardware was
 * a totally silent freeze with 16-px vertical bars, identical on both flavors.
 * (Found 2026-07-23 via the RTL committed-PC histogram: 82% of commits parked
 * on rvstack_trap's first instruction, the rest on this 3-instruction loop.)
 *
 * `v` is volatile so GCC's bswap-idiom pass cannot recognise the shift/or
 * sequence below and "helpfully" turn it back into a call to __bswapsi2.
 */
unsigned __bswapsi2(unsigned x)
{
	volatile unsigned v = x;
	unsigned r = (v & 0x000000FFu) << 24;
	r |= (v & 0x0000FF00u) << 8;
	r |= (v & 0x00FF0000u) >> 8;
	r |= (v & 0xFF000000u) >> 24;
	return r;
}

unsigned long long __fixunssfdi(float a) {
	if (a < 0) return 0;
	unsigned long long r = 0;
	if (a >= 4294967296.0f) {
		unsigned hi = (unsigned)(a / 4294967296.0f);
		a -= (float)hi * 4294967296.0f;
		r = (unsigned long long)hi << 32;
	}
	return r | (unsigned)a;
}
long long __fixsfdi(float a) {
	return a < 0 ? -(long long)__fixunssfdi(-a) : (long long)__fixunssfdi(a);
}
float __floatundisf(unsigned long long a) {
	return (float)(unsigned)(a >> 32) * 4294967296.0f + (float)(unsigned)(a & 0xFFFFFFFFu);
}
float __floatdisf(long long a) {
	return a < 0 ? -__floatundisf((unsigned long long)(-a)) : __floatundisf((unsigned long long)a);
}
