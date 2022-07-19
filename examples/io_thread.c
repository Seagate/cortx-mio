/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <asm/byteorder.h>
#include <pthread.h>
#include <openssl/md5.h>

#include "obj.h"
#include "helpers.h"
#include "io_thread.h"

void
io_thread_generate_data(uint32_t bcount, uint32_t bsize,
			struct mio_iovec *data, MD5_CTX *md5_ctx)
{
	int i;
	int j;
	uint32_t rand;

	for (i = 0; i < bcount; i++) {
		rand = mio_cmd_random(IO_THREAD_MAX_RAND_NUM);
		char *ptr = data[i].miov_base;
		for (j = 0; j < bsize/sizeof(rand); j++) {
			memcpy(ptr, &rand, sizeof(rand));
			ptr += sizeof(rand);
		};
		MD5_Update(md5_ctx, data[i].miov_base, bsize);
	}
}

int io_thread_obj_write(struct mio_obj *obj, uint32_t block_size,
			uint32_t block_count, unsigned char *md5sum)
{
	int rc = 0;
	uint32_t bcount;
	struct mio_iovec *data;
	MD5_CTX md5_ctx;
	uint64_t last_index;
	uint64_t max_index;

	max_index = block_size * block_count;
	last_index = 0;
	MD5_Init(&md5_ctx);
	while (block_count > 0) {
		bcount = (block_count > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)?
			  MIO_CMD_MAX_BLOCK_COUNT_PER_OP : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;
		io_thread_generate_data(bcount, block_size, data, &md5_ctx);

		rc = obj_write(obj, bcount, data);
		if (rc != 0) {
			obj_cleanup_iovecs(data);
			break;
		}

		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}
	MD5_Final(md5sum, &md5_ctx);

	return rc;
}

int io_thread_obj_read(struct mio_obj *obj, uint32_t block_size,
		       uint32_t block_count, unsigned char *md5sum)
{
	int i;
	int rc = 0;
	uint32_t bcount;
	uint64_t last_index;
	uint64_t max_index;
	struct mio_iovec *data;
	MD5_CTX md5_ctx;

	max_index = block_size * block_count;
	last_index = 0;
	MD5_Init(&md5_ctx);
	while (block_count > 0) {
		bcount = (block_count > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)?
			  MIO_CMD_MAX_BLOCK_COUNT_PER_OP : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;

		/* Read data from obj. */
		rc = obj_read(obj, bcount, data);
		if (rc != 0) {
			obj_cleanup_iovecs(data);
			break;
		}

		/* Calculate md5sum from data. */
		for (i = 0; i < bcount; i++)
			MD5_Update(&md5_ctx, data[i].miov_base, block_size);

		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}
	MD5_Final(md5sum, &md5_ctx);

	return rc;
}

void io_threads_stop(pthread_t **threads, int nr_threads)
{
	for (int i = 0; i < nr_threads; i++) {
		if (threads[i] == NULL)
			break;

		mio_cmd_thread_join(threads[i]);
		mio_cmd_thread_fini(threads[i]);
		threads[i] = NULL;
	}
}
