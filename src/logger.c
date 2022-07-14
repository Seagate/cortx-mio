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
#include <time.h>
#include <unistd.h>	
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>
#include <assert.h>

#include "logger.h"
#include "utils.h"

enum {
	MIO_LOG_LEVEL_NAME_MAX = 16
};

struct mio_log_level_name mio_log_levels[] = {
        [MIO_ERROR]	= {"error"},
        [MIO_WARN]	= {"warning"},
        [MIO_INFO]	= {"info"},
        [MIO_TRACE]	= {"trace"},
        [MIO_TELEMETRY]	= {"telem"},
        [MIO_DEBUG]	= {"debug"},
};

enum mio_log_level mio_log_level = MIO_INFO;
FILE *mio_log_file = NULL;
static int bytes_since_flush = 0;

static int log_time(char *time_buf, int len, bool is_fname)
{
	int rc;
	uint64_t now;
	time_t time_secs;
	uint64_t time_nanosecs;
	struct tm tm;

	now = mio_now();
	time_secs = mio_time_seconds(now);
	time_nanosecs = mio_time_nanoseconds(now);
	localtime_r(&time_secs, &tm);

	if (is_fname)
		rc = snprintf(time_buf, len,
			      "%04d-%02d-%02d-%02d-%02d-%02d",
			      1900 + tm.tm_year, tm.tm_mon, tm.tm_mday,
			      tm.tm_hour, tm.tm_min, tm.tm_sec);
	else
		rc = snprintf(time_buf, len,
			      "%04d-%02d-%02d-%02d:%02d:%02d.%09"PRIu64,
			      1900 + tm.tm_year, tm.tm_mon, tm.tm_mday,
			      tm.tm_hour, tm.tm_min, tm.tm_sec, time_nanosecs);
	if (rc > len)
		return -E2BIG;
	else
		return 0;
}

void mio_log(enum mio_log_level lev, const char *fmt, ...)
{
	int rc;
        va_list va;
	char log_rec[MIO_LOG_MAX_REC_LEN];
	char timestamp[MIO_LOG_MAX_TIMESTAMP_LEN];
	char *log_rec_ptr = log_rec;
	int head_len;
	int time_len;
	int log_rec_len;

	va_start(va, fmt);

	head_len = strnlen(mio_log_levels[lev].name, MIO_LOG_LEVEL_NAME_MAX) + 3; 
	assert(head_len < MIO_LOG_MAX_REC_LEN);
	snprintf(log_rec_ptr, head_len, "[%s]", mio_log_levels[lev].name);
	log_rec_ptr += head_len - 1;
	*log_rec_ptr = ' ';
	log_rec_ptr += 1;

	time_len = strnlen(timestamp, MIO_LOG_MAX_TIMESTAMP_LEN);
	rc = log_time(timestamp, MIO_LOG_MAX_TIMESTAMP_LEN, false);
	if (rc < 0 || time_len > MIO_LOG_MAX_REC_LEN - head_len) {
		fprintf(stderr, "Timestamp is too log! How is it possible?\n");
		va_end(va);
		return;
	}
	mio_mem_copy(log_rec_ptr, timestamp, time_len);
	log_rec_ptr += time_len;
	*log_rec_ptr = ' ';
	log_rec_ptr += 1;

	rc = vsnprintf(log_rec_ptr,
		       MIO_LOG_MAX_REC_LEN - (log_rec_ptr - log_rec), fmt, va);
	if (rc >= MIO_LOG_MAX_REC_LEN - (log_rec_ptr - log_rec))
		fprintf(stderr, "Log record is too big!\n");
		
	va_end(va);

	log_rec_len = strnlen(log_rec, MIO_LOG_MAX_REC_LEN);
	rc = fwrite(log_rec, log_rec_len, 1, mio_log_file);
	if (rc != 1) {
		fprintf(stderr, "Failed writing to log file!\n");
		return;
	}
	bytes_since_flush += log_rec_len;
	if (bytes_since_flush < MIO_LOG_MAX_BYTES_TO_FLUSH)
		return;
	fflush(mio_log_file);

	return;
}

static int mio_log_new(FILE **fp, char *logdir)
{
	int rc;
	FILE *new_fp = NULL;
	char log_fname[MIO_LOG_MAX_FNAME_LEN];
	char timestamp[MIO_LOG_MAX_TIMESTAMP_LEN];
	pid_t pid;
	char cwd[PATH_MAX + 1];
	char *dir;
#if defined(_GNU_SOURCE)
	const char *appname = program_invocation_short_name;
#else
	const char *appname = "mio-app";
#endif

	rc = log_time(timestamp, MIO_LOG_MAX_TIMESTAMP_LEN, true);
	if (rc < 0)
		return rc;
	pid = getpid();
	if (logdir == NULL) {
		dir = getcwd(cwd, PATH_MAX);
		if (dir == NULL)
			return -errno;
	} else
		dir = logdir;
	sprintf(log_fname, "%s/%s-%d-%s.log",
		dir, appname, pid, timestamp);

	new_fp = fopen(log_fname, "w+");
	if (new_fp == NULL) {
		fprintf(stderr, "Cann't create new log file: %d\n",
			-errno);
		*fp = NULL;
		return -errno;
	} else {
		*fp = new_fp;
		return 0;
	}
}

int mio_log_init(enum mio_log_level level, char *logdir)
{
	int rc = 0;
	FILE *fp = NULL;

	/*
	 * Create a new log file using the following format:
	 * mio-PID-YY-MM-DAY-TIME.log under the `logdir` directory.
	 * If logdir == NULL, a new log file is created under current
	 * working directory.
	 */
	rc = mio_log_new(&fp, logdir);
	if (rc < 0)
		return rc;

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
