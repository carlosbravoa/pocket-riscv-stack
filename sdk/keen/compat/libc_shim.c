/*
 * libc_shim.c — console-only gap fillers for LiteX's picolibc-minimal
 * (trap #5). Lifted from the Wolf3D port's shim, trimmed to what Omnispeak
 * uses, and with the gamelib overlap REMOVED: gamelib.c now provides
 * malloc/calloc/realloc/free, mem*, and rand/srand — defining those here
 * again would be a duplicate-symbol link error.
 *
 * This libc build ships printf/puts/fputs/vfprintf (over litex_putc) but
 * none of the string-formatting family (v/snprintf, sprintf, sscanf), no
 * strtol/atoi, no ctype, no time. Everything below is absent from
 * libc.a — verified against its symbol table.
 *
 * Part of the Omnispeak riscv-stack port glue. SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "rv_keen.h"

void sys_exit(void); /* HAL: reboot to the game picker */

/* ------------------------------------------------------------ stdlib --- */

void exit(int status)
{
	(void)status;
	RVK_FS_FlushAll(); /* config + savegames reach the SD card */
	sys_exit();
	for (;;) /* not reached */
		;
}

void abort(void)
{
	exit(1);
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

long strtol(const char *s, char **end, int base)
{
	while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
		s++;
	int neg = 0;
	if (*s == '+' || *s == '-')
		neg = (*s++ == '-');
	if ((base == 0 || base == 16) && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
	{
		s += 2;
		base = 16;
	}
	else if (base == 0)
		base = (s[0] == '0') ? 8 : 10;
	long v = 0;
	const char *start = s;
	for (;; s++)
	{
		int d;
		if (*s >= '0' && *s <= '9')
			d = *s - '0';
		else if (*s >= 'a' && *s <= 'z')
			d = *s - 'a' + 10;
		else if (*s >= 'A' && *s <= 'Z')
			d = *s - 'A' + 10;
		else
			break;
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

/* -------------------------------------------------------------- ctype --- */

int toupper(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
int tolower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isprint(int c) { return c >= 0x20 && c < 0x7F; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }

/* ------------------------------------------------------------- string --- */

size_t strlen(const char *); /* picolibc */

char *strstr(const char *h, const char *n)
{
	size_t nl = strlen(n);
	if (!nl)
		return (char *)h;
	for (; *h; h++)
	{
		size_t i = 0;
		while (n[i] && h[i] == n[i])
			i++;
		if (!n[i])
			return (char *)h;
	}
	return 0;
}

char *strcat(char *d, const char *s)
{
	char *r = d;
	d += strlen(d);
	while ((*d++ = *s++))
		;
	return r;
}

char *strrchr(const char *s, int c)
{
	const char *last = 0;
	do
	{
		if (*s == (char)c)
			last = s;
	} while (*s++);
	return (char *)last;
}

void *malloc(size_t); /* gamelib */
void *memcpy(void *, const void *, size_t);
char *strdup(const char *s)
{
	size_t n = strlen(s) + 1;
	char *p = malloc(n);
	if (p)
		memcpy(p, s, n);
	return p;
}

/* --------------------------------------------------------------- time --- */

time_t time(time_t *t)
{
	if (t)
		*t = 0;
	return 0; /* no RTC */
}

struct tm *localtime(const time_t *t)
{
	(void)t;
	static struct tm tm; /* all zeros: Jan 1 1970 00:00 */
	return &tm;
}

/* ------------------------------------------------- formatted output --- */
/* Small printf core (the Tyrian/Wolf3D one): flags - 0, width (and *),
 * precision, lengths hh h l ll z, conversions d i u x X o p c s f n %. */

typedef struct
{
	char *dst;
	size_t cap; /* including NUL */
	size_t len; /* would-be length (snprintf return) */
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
	do
	{
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
	outbuf_t ob = {dst, cap, 0};

	for (; *fmt; fmt++)
	{
		if (*fmt != '%')
		{
			ob_putc(&ob, *fmt);
			continue;
		}
		fmt++;
		if (*fmt == '%')
		{
			ob_putc(&ob, '%');
			continue;
		}
		int left = 0, zero = 0;
		for (;; fmt++)
		{
			if (*fmt == '-')
				left = 1;
			else if (*fmt == '0')
				zero = 1;
			else if (*fmt == '+' || *fmt == ' ' || *fmt == '#')
				;
			else
				break;
		}
		int width = 0;
		if (*fmt == '*')
		{
			width = va_arg(ap, int);
			if (width < 0)
			{
				left = 1;
				width = -width;
			}
			fmt++;
		}
		else
		{
			while (*fmt >= '0' && *fmt <= '9')
				width = width * 10 + (*fmt++ - '0');
		}
		int prec = -1;
		if (*fmt == '.')
		{
			fmt++;
			prec = 0;
			if (*fmt == '*')
			{
				prec = va_arg(ap, int);
				fmt++;
			}
			else
			{
				while (*fmt >= '0' && *fmt <= '9')
					prec = prec * 10 + (*fmt++ - '0');
			}
		}
		int lmod = 0; /* -2 hh, -1 h, 1 l, 2 ll/z */
		while (*fmt == 'h' || *fmt == 'l' || *fmt == 'z')
		{
			if (*fmt == 'h')
				lmod--;
			else if (*fmt == 'l')
				lmod++;
			else
				lmod = 2;
			fmt++;
		}
		switch (*fmt)
		{
		case 'd':
		case 'i':
		{
			long long v = (lmod >= 2) ? va_arg(ap, long long)
				: (lmod == 1)     ? va_arg(ap, long)
						  : va_arg(ap, int);
			if (lmod == -1)
				v = (short)v;
			if (lmod <= -2)
				v = (signed char)v;
			int neg = v < 0;
			ob_num(&ob, neg ? (unsigned long long)-v : (unsigned long long)v,
				10, 0, neg, width, prec, left, zero);
			break;
		}
		case 'u':
		case 'x':
		case 'X':
		case 'o':
		{
			unsigned long long v = (lmod >= 2) ? va_arg(ap, unsigned long long)
				: (lmod == 1)              ? va_arg(ap, unsigned long)
							   : va_arg(ap, unsigned int);
			if (lmod == -1)
				v = (unsigned short)v;
			if (lmod <= -2)
				v = (unsigned char)v;
			int base = (*fmt == 'u') ? 10 : (*fmt == 'o') ? 8 : 16;
			ob_num(&ob, v, base, *fmt == 'X', 0, width, prec, left, zero);
			break;
		}
		case 'p':
		{
			void *p = va_arg(ap, void *);
			ob_str(&ob, "0x", -1, 0, 0, ' ');
			ob_num(&ob, (uintptr_t)p, 16, 0, 0, 0, 8, 0, 0);
			break;
		}
		case 'c':
		{
			char c = (char)va_arg(ap, int);
			if (!left)
				ob_pad(&ob, ' ', width - 1);
			ob_putc(&ob, c);
			if (left)
				ob_pad(&ob, ' ', width - 1);
			break;
		}
		case 's':
		{
			const char *s = va_arg(ap, const char *);
			if (!s)
				s = "(null)";
			ob_str(&ob, s, prec, width, left, zero ? '0' : ' ');
			break;
		}
		case 'f':
		case 'F':
		case 'g':
		case 'G':
		{
			double d = va_arg(ap, double);
			int fneg = d < 0;
			if (fneg)
				d = -d;
			if (prec < 0)
				prec = 6;
			long long ip = (long long)d;
			double frac = d - (double)ip;
			char fb[16];
			int fn = 0;
			for (int i = 0; i < prec && fn < (int)sizeof(fb) - 1; i++)
			{
				frac *= 10.0;
				int dig = (int)frac;
				fb[fn++] = (char)('0' + dig);
				frac -= dig;
			}
			fb[fn] = 0;
			ob_num(&ob, ip, 10, 0, fneg, 0, -1, 0, 0);
			if (prec > 0)
			{
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

/* picolibc has vfprintf (litex console) but not vprintf (ck_cross.c). */
int vprintf(const char *fmt, va_list ap)
{
	return vfprintf(stdout, fmt, ap);
}

/* -------------------------------------------------- formatted input --- */
/* Mini sscanf: whitespace skipping, literal matching, %d %i %u %x %c %n
 * (with optional width) — Omnispeak only uses "%d". */

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

	for (; *fmt; fmt++)
	{
		if (*fmt == ' ' || *fmt == '\t' || *fmt == '\n')
		{
			while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
				s++;
			continue;
		}
		if (*fmt != '%')
		{
			if (*s != *fmt)
				return stored;
			s++;
			continue;
		}
		fmt++;
		if (*fmt == '%')
		{
			if (*s != '%')
				return stored;
			s++;
			continue;
		}
		int width = 0;
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		while (*fmt == 'h' || *fmt == 'l')
			fmt++;
		long v;
		switch (*fmt)
		{
		case 'd':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
				s++;
			if (!scan_int(&s, &v, 10))
				return stored;
			*va_arg(ap, int *) = (int)v;
			stored++;
			break;
		case 'i':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
				s++;
			if (!scan_int(&s, &v, 0))
				return stored;
			*va_arg(ap, int *) = (int)v;
			stored++;
			break;
		case 'u':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
				s++;
			if (!scan_int(&s, &v, 10) || v < 0)
				return stored;
			*va_arg(ap, unsigned *) = (unsigned)v;
			stored++;
			break;
		case 'x':
			while (*s == ' ' || (*s >= '\t' && *s <= '\r'))
				s++;
			if (!scan_int(&s, &v, 16))
				return stored;
			*va_arg(ap, unsigned *) = (unsigned)v;
			stored++;
			break;
		case 'c':
		{
			if (width == 0)
				width = 1;
			char *dst = va_arg(ap, char *);
			for (int i = 0; i < width; i++)
			{
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

/* ------------------------------------------------------------- misc --- */

char *getenv(const char *name)
{
	(void)name; /* no environment on the console */
	return 0;
}

/* libc has vfprintf (litex console) but ships no fprintf wrapper. */
int fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	int n = vfprintf(f, fmt, ap);
	va_end(ap);
	return n;
}
