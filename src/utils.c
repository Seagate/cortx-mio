/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdlib.h>
#include <sys/time.h>
#include <asm/byteorder.h>

#include "mio_internal.h"

void *mio_mem_alloc(size_t size)
{
	void *p;

	p = malloc(size);
	if (p != NULL)
		memset(p, 0, size);

	return p;
}

void mio_mem_free(void *p)
{
	if (p == NULL)
		return;
	free(p);
}

void mio_memset(void *p, int c, size_t size)
{
	memset(p, c, size);
}

void mio_mem_copy(void *to, void *from, size_t size)
{
	memcpy(to, from, size);
}

enum {
        TIME_ONE_SECOND = 1000000000ULL,
        TIME_ONE_MSEC   = TIME_ONE_SECOND / 1000
};

uint64_t mio_now()
{
        struct timeval tv;

        gettimeofday(&tv, NULL);
        return tv.tv_sec * TIME_ONE_SECOND + tv.tv_usec * 1000;
}

uint64_t mio_byteorder_cpu_to_be64(uint64_t cpu_64bits)
{
        return __cpu_to_be64(cpu_64bits);
}

uint64_t mio_byteorder_be64_to_cpu(uint64_t big_endian_64bits)
{
        return __be64_to_cpu(big_endian_64bits);
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
