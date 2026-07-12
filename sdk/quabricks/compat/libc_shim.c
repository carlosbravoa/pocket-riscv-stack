/*
 * libc_shim.c — the picolibc-minimal gaps quabricks actually hits.
 * CONSOLE BUILD ONLY (the PC twin links the real libc; see Makefile).
 *
 * LiteX links picolibc-MINIMAL: printf/puts/fputs to the UART, str*, mem*
 * (gamelib overrides mem* word-wide and adds malloc/rand) — but NO
 * v*printf and no snprintf (PORTABILITY.md trap #5). The game's HUD is
 * built with snprintf, so provide a small printf core for it. Trimmed from
 * sdk/tyrian/compat/libc_shim.c (the port that paid for it): %d %i %u %x
 * %c %s %% with flags/width/precision — everything tetris.c formats.
 *
 * MIT (port glue; inherits the game's license, see ../LICENSE).
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

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

static void ob_str(outbuf_t *ob, const char *s, int max, int width, int left,
                   char padc)
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

static void ob_num(outbuf_t *ob, unsigned long v, int base, int upper,
                   int neg, int width, int prec, int left, int zero)
{
	char tmp[16];
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
		while (*fmt >= '0' && *fmt <= '9')
			width = width * 10 + (*fmt++ - '0');
		int prec = -1;
		if (*fmt == '.') {
			fmt++;
			prec = 0;
			while (*fmt >= '0' && *fmt <= '9')
				prec = prec * 10 + (*fmt++ - '0');
		}
		while (*fmt == 'h' || *fmt == 'l')  /* length mods: int-sized here */
			fmt++;
		switch (*fmt) {
		case 'd': case 'i': {
			long v = va_arg(ap, int);
			int neg = v < 0;
			ob_num(&ob, neg ? (unsigned long)-v : (unsigned long)v,
			       10, 0, neg, width, prec, left, zero);
			break;
		}
		case 'u': case 'x': case 'X': {
			unsigned long v = va_arg(ap, unsigned int);
			ob_num(&ob, v, (*fmt == 'u') ? 10 : 16, *fmt == 'X',
			       0, width, prec, left, zero);
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
			ob_str(&ob, s, prec, width, left, ' ');
			break;
		}
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
