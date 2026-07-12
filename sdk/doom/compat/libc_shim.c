/*
 * libc_shim.c — minimal libc pieces for the DOOM riscv-stack port.
 * CONSOLE BUILD ONLY (the PC twin uses the real libc).
 *
 * LiteX links picolibc-MINIMAL: only ~28 symbols (printf/vfprintf to the
 * UART, str(n)cmp/cpy, strchr, strtoul, mem*), and gamelib.c adds
 * malloc/free/calloc/realloc/memcmp. Everything else Doom links against is
 * implemented here, smallest-correct-thing style. Derived from
 * sdk/tyrian/compat/libc_shim.c; Doom deltas:
 *
 *   vsscanf grows %s and %[set] (m_config reads "%79s %99[^\n]"),
 *   string:  strdup, strcasecmp/strncasecmp, strstr,
 *   stdlib:  atof (m_config float defaults), getenv (=NULL: no environment),
 *            system (=-1: no shell — disarms i_system.c's zenity probe).
 *
 * GPL-2.0-or-later (port glue; see ../ATTRIBUTION.md).
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

static unsigned long rand_state = 0x2545F491uL;

void srand(unsigned seed) { rand_state = seed ? seed : 1; }

int rand(void)
{
	rand_state = rand_state * 1103515245uL + 12345uL;
	return (int)((rand_state >> 16) & 0x7FFF);
}

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

double atof(const char *s)
{
	while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
		s++;
	int neg = 0;
	if (*s == '+' || *s == '-')
		neg = (*s++ == '-');
	double v = 0.0;
	while (*s >= '0' && *s <= '9')
		v = v * 10.0 + (*s++ - '0');
	if (*s == '.') {
		s++;
		double scale = 0.1;
		while (*s >= '0' && *s <= '9') {
			v += (*s++ - '0') * scale;
			scale *= 0.1;
		}
	}
	return neg ? -v : v;
}

char *strerror(int errnum)
{
	(void)errnum;
	return (char *)"error";             /* only feeds warning printf()s */
}

char *getenv(const char *name)
{
	(void)name;                         /* no environment on the console */
	return 0;
}

int system(const char *cmd)
{
	(void)cmd;                          /* no shell: zenity probe says no */
	return -1;
}

/* ------------------------------------------------------------ string --- */

size_t strlen(const char *);            /* picolibc */
char *strcpy(char *, const char *);
int strncmp(const char *, const char *, size_t);
void *memcpy(void *, const void *, size_t);
void *malloc(size_t);

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

char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = malloc(n);
	if (d)
		memcpy(d, s, n);
	return d;
}

static int lc(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

int strcasecmp(const char *a, const char *b)
{
	while (*a && lc(*a) == lc(*b)) {
		a++;
		b++;
	}
	return lc(*(const unsigned char *)a) - lc(*(const unsigned char *)b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
	for (; n; n--, a++, b++) {
		int d = lc(*(const unsigned char *)a) - lc(*(const unsigned char *)b);
		if (d || !*a)
			return d;
	}
	return 0;
}

char *strstr(const char *h, const char *n)
{
	size_t nl = strlen(n);
	if (!nl)
		return (char *)h;
	for (; *h; h++)
		if (*h == *n && strncmp(h, n, nl) == 0)
			return (char *)h;
	return 0;
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
	return 0;                           /* no RTC */
}

struct tm *localtime(const time_t *t)
{
	(void)t;
	static struct tm tm;                /* all zeros: Jan 1 1970 00:00 */
	return &tm;
}

/* ------------------------------------------------- formatted output --- */
/* Small printf core: flags - 0, width (and *), precision, lengths hh h l
 * ll z, conversions d i u x X o p c s f n %. (Tyrian's, unchanged.) */

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
 * plus (Doom) %s and %[set], all with optional width. m_config's line
 * parse is "%79s %99[^\n]". Returns the number of conversions stored. */

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
		/* width (honored for %c/%s/%[) */
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
		case 's': {
			while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
				s++;
			if (!*s)
				return stored;
			char *dst = va_arg(ap, char *);
			int i = 0;
			while (*s && !(*s == ' ' || (*s >= '\t' && *s <= '\r'))
			       && (width == 0 || i < width))
				dst[i++] = *s++;
			dst[i] = 0;
			if (i == 0)
				return stored;
			stored++;
			break;
		}
		case '[': {                     /* %[set] / %[^set] */
			fmt++;
			int negate = (*fmt == '^');
			if (negate)
				fmt++;
			const char *set = fmt;
			if (*fmt == ']')            /* ']' first = literal member */
				fmt++;
			while (*fmt && *fmt != ']')
				fmt++;
			size_t setlen = (size_t)(fmt - set);
			char *dst = va_arg(ap, char *);
			int i = 0;
			while (*s && (width == 0 || i < width)) {
				int in_set = 0;
				for (size_t k = 0; k < setlen; k++)
					if (*s == set[k]) {
						in_set = 1;
						break;
					}
				if (in_set == negate)
					break;
				dst[i++] = *s++;
			}
			dst[i] = 0;
			if (i == 0)
				return stored;
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

// picolibc-minimal has no libm: fabs for v_video's dev overlay
double fabs(double x) { return x < 0 ? -x : x; }
