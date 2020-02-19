/* -*- C -*- */
/*
 */

#pragma once

#ifndef __MIO_LOGGER_H__
#define __MIO_LOGGER_H__

#include <stdarg.h>       /* va_list */
#include <stdio.h>        /* vfprintf(), stderr */

enum mio_log_level {
	MIO_LOG_INVALID = -1,
	MIO_ERROR	= 0,
	MIO_WARN	= 1,
	MIO_INFO	= 2,
	MIO_TRACE	= 3,
	MIO_DEBUG	= 4,
	MIO_MAX_LEVEL,
};

int mio_log_init(enum mio_log_level level, char *logfile);
void mio_log(enum mio_log_level lev, const char *fmt, ...)
	__attribute__((format(printf, 2, 3)));
//void mio_set_debug_level(enum mio_log_level level);

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
