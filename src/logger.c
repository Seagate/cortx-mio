/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <errno.h>
#include <string.h>

#include "logger.h"
#include "mio_internal.h"

enum mio_log_level mio_log_level = MIO_INFO;

struct {
	const char *name;
} mio_log_levels[] = {
        [MIO_ERROR]	= {"error"},
        [MIO_WARN]	= {"warning"},
        [MIO_INFO]	= {"info"},
        [MIO_TRACE]	= {"trace"},
        [MIO_DEBUG]	= {"debug"},
};

static FILE *mio_log_file;

void mio_log(enum mio_log_level lev, const char *fmt, ...)
{
        va_list va;
	va_start(va, fmt);
	fprintf(mio_log_file, "%s: ", mio_log_levels[lev].name);
	vfprintf(mio_log_file, fmt, va);
	va_end(va);

}

int mio_log_init(enum mio_log_level level, char *logfile)
{
	FILE *fp;	

	if (logfile != NULL) {
		fp = fopen(logfile, "w");
		if (fp == NULL) {
			fprintf(stderr,
				"Cann't open log file %s (%d)\n", logfile, errno);
			return -errno;
		}
	} else
		fp = stderr; 

	mio_log_level = level;
	mio_log_file  = fp;
	return 0;
}

void mio_log_fini()
{
	fclose(mio_log_file);
}

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
