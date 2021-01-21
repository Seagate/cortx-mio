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
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <asm/byteorder.h>
#include <pthread.h>

#include "obj.h"
#include "helpers.h"

/**
 * The object IO examples in this file show how to use MIO APIs
 * in a blocking fashion. After an operation is created, we wait until
 * an operation completed or failed by using mio_op_poll().
 *
 * Of course, applications can also create (and launch internally
 * in MIO) operations and poll the operations in a dedicated thread.
 */

int obj_write(struct mio_obj *obj, uint32_t bcount, struct mio_iovec *data)
{
	int rc;
	struct mio_op op;

	mio_op_init(&op);
	rc = mio_obj_writev(obj, data, bcount, &op);
	if (rc < 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in writing to object!\n");
	mio_op_fini(&op);
	return rc;
}

int obj_read(struct mio_obj *obj, uint32_t bcount, struct mio_iovec *data)
{
	int rc;
	struct mio_op op;

	mio_op_init(&op);
	rc = mio_obj_readv(obj, data, bcount, &op);
	if (rc < 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in reading from object!\n");
	mio_op_fini(&op);
	return rc;
}

/*
 * Create an object and copy data from a file to the newly created object.
 */
int mio_cmd_obj_write(char *src, struct mio_pool_id *pool,
		      struct mio_obj_id *oid,
		      uint32_t block_size, uint32_t block_count)
{
	int rc = 0;
	uint32_t bcount;
	uint64_t last_index;
	uint64_t max_index;
	uint64_t max_block_count;
	struct mio_iovec *data;
	struct mio_obj obj;
	FILE *fp = NULL;
	struct stat src_stat;

	/* If `src` file is not set, use pseudo data. */
	if (src == NULL) {
		max_index = block_count * block_size;
		goto create_obj;
	}

	/* Open source file */
	fp = fopen(src, "r");
	if (fp == NULL)
		return -errno;

	rc = fstat(fileno(fp), &src_stat);
	if (rc < 0)
		goto src_close;
	max_index = src_stat.st_size;
	max_block_count = (max_index - 1) / block_size + 1;
	block_count = block_count > max_block_count?
		      max_block_count : block_count;

create_obj:
	/* Create the target object if it doesn't exist. */
	memset(&obj, 0, sizeof obj);
	rc = obj_open_or_create(pool, oid, &obj, NULL);
	if (rc < 0)
		goto src_close;

	last_index = 0;
	while (block_count > 0) {
		bcount = (block_count > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)?
			  MIO_CMD_MAX_BLOCK_COUNT_PER_OP : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;

		/* Read data from source file. */
		rc = obj_read_data_from_file(fp, bcount, block_size, data);
		if (rc != bcount) {
			fprintf(stderr, "Failed in reading from file!\n");
			obj_cleanup_iovecs(data);
			break;
		}

		/* Copy data to the object*/
		rc = obj_write(&obj, bcount, data);
		if (rc != 0) {
			fprintf(stderr, "Writing to object failed!\n");
			obj_cleanup_iovecs(data);
			break;
		}
		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}

	mio_obj_close(&obj);

src_close:
	if (fp)
		fclose(fp);
	return rc;
}

/*
 * Read an object and output to a file or stderr.
 */
int mio_cmd_obj_read(struct mio_obj_id *oid, char *dest,
		     uint32_t block_size, uint32_t block_count)
{
	int rc = 0;
	uint32_t bcount;
	uint64_t last_index;
	uint64_t max_index;
	uint64_t max_block_count;
	struct mio_iovec *data;
	struct mio_obj obj;
	FILE *fp = NULL;

	if (dest != NULL) {
		fp = fopen(dest, "w");
		if (fp == NULL)
			return -errno;

	}

	memset(&obj, 0, sizeof obj);
	rc = obj_open(oid, &obj);
	if (rc < 0)
		goto dest_close;
	max_index = obj.mo_attrs.moa_size;
	max_block_count = (max_index - 1) / block_size + 1;
	block_count = block_count > max_block_count?
		      max_block_count : block_count;

	last_index = 0;
	while (block_count > 0) {
		bcount = (block_count > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)?
			  MIO_CMD_MAX_BLOCK_COUNT_PER_OP : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;

		/* Read data from obj. */
		rc = obj_read(&obj, bcount, data);
		if (rc != 0) {
			fprintf(stderr, "Failed in reading from file!\n");
			obj_cleanup_iovecs(data);
			break;
		}

		/* Copy data to the file. */
		rc = obj_write_data_to_file(fp, bcount, data);
		if (rc != bcount) {
			fprintf(stderr, "[mio_cmd_obj_read] "
					"Writing to output file failed!\n");
			obj_cleanup_iovecs(data);
			break;
		}
		rc = 0;

		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}

	mio_obj_close(&obj);

dest_close:
	if(fp != NULL)
		fclose(fp);
	return rc;
}

/*
 * Copy from one object to another. A new object will be created
 * if the `to` object doesn't exist. This function also shows how to use
 * MIO's hint such as MIO_HINT_OBJ_OBJ_WHERE or MIO_HINT_OBJ_HOT_INDEX
 * to create an object in a specified pool.
 */
int mio_cmd_obj_copy(struct mio_obj_id *from_oid,
		     struct mio_pool_id *to_pool, struct mio_obj_id *to_oid,
		     uint32_t block_size, uint32_t block_count,
		     struct mio_cmd_obj_hint *chint)
{
	int rc = 0;
	uint32_t bcount;
	uint64_t last_index;
	uint64_t max_index;
	uint64_t max_block_count;
	struct mio_iovec *data;
	struct mio_obj from_obj;
	struct mio_obj to_obj;

	/* Open `from` object. */
	memset(&from_obj, 0, sizeof from_obj);
	rc = obj_open(from_oid, &from_obj);
	if (rc < 0)
		goto obj_close;
	max_index = from_obj.mo_attrs.moa_size;
	max_block_count = (max_index - 1) / block_size + 1;
	block_count = block_count > max_block_count?
		      max_block_count : block_count;

	/* Create the `to` object if it doesn't exist. */
	memset(&to_obj, 0, sizeof to_obj);
	rc = obj_open_or_create(to_pool, to_oid, &to_obj, chint);
	if (rc < 0)
		goto obj_close;

	last_index = 0;
	while (block_count > 0) {
		bcount = (block_count > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)?
			  MIO_CMD_MAX_BLOCK_COUNT_PER_OP : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;

		/* Read data from source file. */
		rc = obj_read(&from_obj, bcount, data);
		if (rc != 0) {
			fprintf(stderr, "Failed in reading from object!\n");
			obj_cleanup_iovecs(data);
			break;
		}

		/* Copy data to the object*/
		rc = obj_write(&to_obj, bcount, data);
		if (rc != 0) {
			fprintf(stderr, "Writing to object failed!\n");
			obj_cleanup_iovecs(data);
			break;
		}
		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}

	mio_obj_close(&to_obj);

obj_close:
	mio_obj_close(&from_obj);
	return rc;
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
