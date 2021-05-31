#ifndef _WAYLOGOUT_LOG_H
#define _WAYLOGOUT_LOG_H

#include <stdarg.h>
#include <string.h>
#include <errno.h>

enum log_importance {
	LOG_SILENT = 0,
	LOG_ERROR = 1,
	LOG_INFO = 2,
	LOG_DEBUG = 3,
	LOG_TRACE = 4,
	LOG_IMPORTANCE_LAST,
};

void waylogout_log_init(enum log_importance verbosity);

#ifdef __GNUC__
#define _ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _ATTRIB_PRINTF(start, end)
#endif

void _waylogout_log(enum log_importance verbosity, const char *format, ...)
	_ATTRIB_PRINTF(2, 3);

void _waylogout_trace(const char *file, int line, const char *func);

const char *_waylogout_strip_path(const char *filepath);

#define waylogout_log(verb, fmt, ...) \
	_waylogout_log(verb, "[%s:%d] " fmt, _waylogout_strip_path(__FILE__), \
			__LINE__, ##__VA_ARGS__)

#define waylogout_log_errno(verb, fmt, ...) \
	waylogout_log(verb, fmt ": %s", ##__VA_ARGS__, strerror(errno))

#define waylogout_trace() \
	_waylogout_trace(__FILE__, __LINE__, __func__)

#endif
