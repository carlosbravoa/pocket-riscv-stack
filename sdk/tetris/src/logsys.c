#include "logsys.h"

#include <stdio.h>

/* RVSTACK: upstream logged to a FILE* ("error.log"). The console has no
 * writable filesystem (and LiteX's picolibc has no fopen) — route the
 * messages to printf instead, which the SDK wires to the debug UART
 * (sdk/GUIDE.md); on the PC twin they land on stdout. TRACE/DEBUG are
 * dropped: per-piece spam is real time on a 115200 UART. */

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
	(void)filename;
	log_on = 1;
}

void log_close() {
	log_on = 0;
}

void log_msgf(int level, const char *format, ...) {
	if(!log_on || logLevel > level) return;
	printf("%s ", levelStr[level]);
	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}
