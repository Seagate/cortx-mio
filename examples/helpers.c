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
#include <asm/byteorder.h>
#include <pthread.h>

#include "src/mio.h"

int mio_cmd_obj_id_clone(struct mio_obj_id *orig_oid,
			 struct mio_obj_id *new_oid,
			 int off1, int off2)
{
	uint64_t u1;
	uint64_t u2;
	uint64_t n1;
	uint64_t n2;

	if (orig_oid == NULL || new_oid == NULL)
		return -EINVAL;

	memcpy(&u1, orig_oid->moi_bytes, sizeof u1);
	memcpy(&u2, orig_oid->moi_bytes + sizeof u1, sizeof u2);
	u1 = __be64_to_cpu(u1);
	u2 = __be64_to_cpu(u2);

	n1 = u1 + off1;
	n2 = u2 + off2;
	n1 = __cpu_to_be64(n1);
	n2 = __cpu_to_be64(n2);
	memcpy(new_oid->moi_bytes, &n1, sizeof n1);
	memcpy(new_oid->moi_bytes + sizeof n1, &n2, sizeof n2);
	return 0;
}

int mio_cmd_id_sscanf(char *idstr, uint64_t *out_u1, uint64_t *out_u2)
{
        int rc;
        int n;
	uint64_t u1;
	uint64_t u2;

	if (out_u1 == NULL || out_u2 == NULL)
		return -EINVAL;

        rc = sscanf(idstr, "%"SCNx64" : %"SCNx64" %n", &u1, &u2, &n);
        if (rc < 0)
                return rc;
        u1 = __cpu_to_be64(u1);
        u2 = __cpu_to_be64(u2);

        memcpy(out_u1, &u1, sizeof u1);
        memcpy(out_u2, &u2, sizeof u2);
        return 0;
}


int mio_cmd_strtou64(const char *arg, uint64_t *out)
{
	char *end = NULL;
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

	if (*end != 0) {
		char *pos = strchr(suffix, *end);
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
	void *result;

	if (pthread_join(*th, &result) == 0) {
		int rc = 0;
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
