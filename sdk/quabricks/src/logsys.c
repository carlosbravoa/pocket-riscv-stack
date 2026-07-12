#include "logsys.h"

#include <stdio.h>

/* RVSTACK: upstream logged to a FILE* ("error.log") through vfprintf. The
 * console has no writable filesystem and LiteX's picolibc-minimal has NO
 * v*printf at all (PORTABILITY.md trap #5) — route messages to stdout with
 * the fputs pattern instead (level + format string, arguments dropped);
 * that lands on the debug UART on the console and the terminal on the PC
 * twin. TRACE/DEBUG are filtered: per-piece spam is real time at 115200. */

int logLevel = INFO;

char *levelStr[7] = {
	"[ALL]",
	"[TRACE]",
	"[DEBUG]",
	"[INFO]",
	"[WARNING]",
	"[ERROR]",
	"[FATAL]"
};

static int log_on = 0;

void log_open(const char *filename) {
	(void)filename;                     /* RVSTACK: no file behind this */
	log_on = 1;
}

void log_close() {
	log_on = 0;
}

void log_msgf(int level, const char *format, ...) {
	if(!log_on || logLevel > level) return;
	fputs(levelStr[level], stdout);
	fputs(" ", stdout);
	fputs(format, stdout);              /* RVSTACK: no v*printf — the format
	                                     * string alone still names the event */
	va_list args;
	va_start(args, format);
	(void)args;
	va_end(args);
}
