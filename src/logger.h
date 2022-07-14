/* -*- C -*- */
/*
 */

#pragma once

#ifndef __MIO_LOGGER_H__
#define __MIO_LOGGER_H__

#include <stdarg.h>       /* va_list */
#include <stdio.h>        /* vfprintf(), stderr */

enum {
	MIO_LOG_MAX_FNAME_LEN = 256,
	MIO_LOG_MAX_REC_LEN = 1024,
	MIO_LOG_MAX_TIMESTAMP_LEN = 64,
	MIO_LOG_MAX_BYTES_TO_FLUSH = 4 * 1024 * 1024,
	MIO_LOG_DIR_PATH_MAX = 4096
};

enum mio_log_level {
	MIO_LOG_INVALID = -1,
	MIO_ERROR	= 0,
	MIO_WARN	= 1,
	MIO_INFO	= 2,
	MIO_TRACE	= 3,
	MIO_TELEMETRY   = 4,
	MIO_DEBUG	= 5,
	MIO_MAX_LEVEL,
};

struct mio_log_level_name {
	const char *name;
};
extern struct mio_log_level_name mio_log_levels[];

extern enum mio_log_level mio_log_level;
extern FILE *mio_log_file;

/**
 * Logging APIs.
 */
int mio_log_init(enum mio_log_level level, char *logfile);
void mio_log(enum mio_log_level lev, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));

#define mio_log_ex(lev, fmt, args...) \
	mio_log(lev, "%s:%d "fmt, __FUNCTION__, __LINE__, ##args)

#endif /* __MIO_LOGGER_H__ */

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
/*
 * vim: tabstop=8 shiftwidth=8 noexpandtab textwidth=80 nowrap
 */
