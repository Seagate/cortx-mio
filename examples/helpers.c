/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <string.h>     
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <errno.h>
#include <pthread.h>

#include "src/mio.h"

int mio_cmd_strtou64(const char *arg, uint64_t *out)
{
	char *end = NULL;
	char *pos;
	static const char suffix[] = "bkmgKMG";
	int rc = 0;

	static const uint64_t multiplier[] = {
		1 << 9,
		1 << 10,
		1 << 20,
		1 << 30,
		1000,
		1000 * 1000,
		1000 * 1000 * 1000
	};

	*out = strtoull(arg, &end, 0);

	if (*end != 0 && rc == 0) {
		pos = strchr(suffix, *end);
		if (pos != NULL) {
			if (*out <= UINT64_MAX / multiplier[pos - suffix])
				*out *= multiplier[pos - suffix];
			else
				rc = -EOVERFLOW;
		} else
			rc = -EINVAL;
	}
	return rc;
}

int mio_cmd_wait_on_op(struct mio_op *op)
{
	struct mio_pollop pop;

	memset(&pop, 0, sizeof pop);
	pop.mp_op = op;
	mio_op_poll(&pop, 1, MIO_TIME_NEVER);
	return op->mop_rc;
}

uint32_t mio_cmd_random(uint32_t max)
{
	return (uint32_t)random() % max;
}

void mio_cmd_error(char *msg, int error)
{
	fprintf(stderr, "%s: errno = %d, %s\n", msg, error, strerror(-error));
}

int mio_cmd_thread_init(pthread_t **ret_th, void* (*func)(void *), void *args)
{
	int rc;
	pthread_t *th;

	th = (pthread_t *) malloc(sizeof(*th));
	if (th == NULL)
		return -ENOMEM;

	rc = pthread_create(th, NULL, func, args);
	if (rc == 0)
		*ret_th = th;

	return -rc;
}

int mio_cmd_thread_join(pthread_t *th)
{
	int rc = 0;
	void *result;

	if (pthread_join(*th, &result) == 0) {
		if (result) {
			rc = *((int *)result);
			free(result);
		}
		return rc;
	} else
		return -1;
}

void mio_cmd_thread_fini(pthread_t *th)
{
	free(th);
	return;
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
