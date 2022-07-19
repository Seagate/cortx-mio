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
 * obj_io_cbs.c gives examples on how to use MIO callback functions
 * for object READ/WRITE.
 */

struct obj_io_cb_args {
	int ica_bcount;
	struct mio_iovec *ica_iovecs;
	FILE *ica_fp;
};

pthread_cond_t obj_io_cond;
pthread_mutex_t obj_io_mutex;
static int obj_io_nr_ops_on_fly = 0;
static int obj_io_nr_ops_todo = 0;
static struct mio_op **obj_io_ops;

void obj_wait_on_all_ops()
{
	pthread_mutex_lock(&obj_io_mutex);
	while(obj_io_nr_ops_on_fly != 0)
		pthread_cond_wait(&obj_io_cond, &obj_io_mutex);
	pthread_mutex_unlock(&obj_io_mutex);

}

static int
obj_rw_op_alloc(struct mio_op **op, struct obj_io_cb_args **args,
		FILE *fp, uint64_t bcount, struct mio_iovec *data)
{
	if (op == NULL || args == NULL)
		return -EINVAL;
	*args = malloc(sizeof **args);
	if (*args == NULL)
		return -ENOMEM;
	/*
 	 * Allocate memory for op as the op's memory must keep
 	 * alive after exiting this function.
 	 */
	*op = mio_op_alloc_init();
	if (*op == NULL) {
		free(*args);
		return -ENOMEM;
	}

	(*args)->ica_bcount = bcount;
	(*args)->ica_iovecs = data;
	(*args)->ica_fp = fp;
	return 0;
}

void obj_write_cb(struct mio_op *op)
{
	struct obj_io_cb_args *args;

	args = (struct obj_io_cb_args *)op->mop_app_cbs.moc_cb_data;
	obj_cleanup_iovecs(args->ica_iovecs);
	free(args);

	pthread_mutex_lock(&obj_io_mutex);
	obj_io_nr_ops_on_fly--;
	pthread_cond_signal(&obj_io_cond);
	pthread_mutex_unlock(&obj_io_mutex);
}

static int obj_write_async(struct mio_obj *obj, int op_idx,
			   uint64_t bcount, struct mio_iovec *data)
{
	int rc;
	struct mio_op *op;
	struct obj_io_cb_args *args;

	rc = obj_rw_op_alloc(&op, &args, NULL, bcount, data);
	if (rc < 0)
		return rc;

	/*
	 * Callbacks must be set before calling mio_obj_writev() as
	 * the op will be launched in this function. Same to other
	 * MIO APIs if one wants to set callbacks.
	 */
	mio_op_callbacks_set(op, obj_write_cb, obj_write_cb, args);
	rc = mio_obj_writev(obj, data, bcount, op);
	if (rc < 0) {
		mio_op_fini_free(op);
		free(args);
	} else
		obj_io_ops[op_idx] = op;
	return rc;
}

void obj_read_cb(struct mio_op *op)
{
	struct obj_io_cb_args *args;

	args = (struct obj_io_cb_args *)op->mop_app_cbs.moc_cb_data;
	if (op->mop_rc == 0)
		obj_write_data_to_file(
			args->ica_fp, args->ica_bcount, args->ica_iovecs);

	obj_cleanup_iovecs(args->ica_iovecs);
	free(args);

	pthread_mutex_lock(&obj_io_mutex);
	obj_io_nr_ops_on_fly--;
	pthread_cond_signal(&obj_io_cond);
	pthread_mutex_unlock(&obj_io_mutex);
}

static int
obj_read_async(struct mio_obj *obj, FILE *write_to_fp,
	       int op_idx, uint64_t bcount, struct mio_iovec *data)
{
	int rc;
	struct mio_op *op;
	struct obj_io_cb_args *args;

	rc = obj_rw_op_alloc(&op, &args, write_to_fp, bcount, data);
	if (rc < 0)
		return rc;
	mio_op_callbacks_set(op, obj_read_cb, obj_read_cb, args);
	rc = mio_obj_readv(obj, data, bcount, op);
	if (rc < 0) {
		mio_op_fini_free(op);
		free(args);
	} else
		obj_io_ops[op_idx] = op;
	return rc;
}

static int obj_io_async_init(int bcount, uint64_t async_step)
{
	obj_io_nr_ops_todo = (bcount + async_step - 1) / async_step;
	obj_io_ops = malloc(obj_io_nr_ops_todo * sizeof(struct mio_op *));
	if (obj_io_ops == NULL)
		return -ENOMEM;
	memset(obj_io_ops, 0, obj_io_nr_ops_todo * sizeof(struct mio_op *));

	pthread_cond_init(&obj_io_cond, NULL);
	pthread_mutex_init(&obj_io_mutex, NULL);

	return 0;
}

static void obj_io_async_fini()
{
	int i;

	pthread_mutex_destroy(&obj_io_mutex);
	pthread_cond_destroy(&obj_io_cond);

	for (i = 0; i < obj_io_nr_ops_todo; i++) {
		if (obj_io_ops[i] != NULL) {
			mio_op_fini(obj_io_ops[i]);
			free(obj_io_ops[i]);
		}
	}

	free(obj_io_ops);
}

int mio_cmd_obj_write_async(char *src, struct mio_pool_id *pool,
			    struct mio_obj_id *oid,
			    uint64_t block_size, uint64_t block_count,
			    uint64_t async_step)
{
	int rc = 0;
	uint64_t bcount;
	uint64_t last_index;
	uint64_t max_index;
	uint64_t max_block_count;
	struct mio_iovec *data;
	struct mio_obj obj;
	FILE *fp = NULL;

	rc = obj_write_init(pool, oid, &obj, src,
			    block_size, &block_count,
			    &max_index, &max_block_count, &fp);
	if (rc < 0)
		return rc;

	rc = obj_io_async_init(block_count, async_step);
	if (rc < 0)
		goto obj_close;

	pthread_mutex_lock(&obj_io_mutex);
	last_index = 0;
	if (async_step > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)
		async_step = MIO_CMD_MAX_BLOCK_COUNT_PER_OP;
	while (block_count > 0) {
		bcount = (block_count > async_step)? async_step : block_count;
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
		rc = obj_write_async(&obj, obj_io_nr_ops_on_fly, bcount, data);
		if (rc != 0) {
			fprintf(stderr, "Writing to object failed!\n");
			obj_cleanup_iovecs(data);
			break;
		}
		block_count -= bcount;
		last_index += bcount * block_size;

		++obj_io_nr_ops_on_fly;
	}
	pthread_mutex_unlock(&obj_io_mutex);

	obj_wait_on_all_ops();

	obj_io_async_fini();

obj_close:
	mio_obj_close(&obj);
	fclose(fp);
	return rc;
}

int mio_cmd_obj_read_async(struct mio_obj_id *oid, char *dest,
			   uint64_t block_size, uint64_t block_count,
			   uint64_t async_step)
{
	int rc = 0;
	uint64_t bcount;
	uint64_t last_index;
	uint64_t max_index;
	uint64_t max_block_count;
	struct mio_iovec *data;
	struct mio_obj obj;
	FILE *fp = NULL;

	/*
	 * As 'obj' keeps alive in the scope of this function till
	 * all operation are done, so it is ok not to allocate memory
	 * for 'obj' here. Make sure that 'obj' is kept alive until
	 * all the ops which are doing IO on it are done.
	 */
	memset(&obj, 0, sizeof obj);
	rc = obj_read_init(oid, &obj, dest,
			   block_size, &block_count,
			   &max_index, &max_block_count, &fp);
	if (rc < 0)
		return rc;

	rc = obj_io_async_init(block_count, async_step);
	if (rc < 0)
		goto obj_close;

	pthread_mutex_lock(&obj_io_mutex);
	last_index = 0;
	if (async_step > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)
		async_step = MIO_CMD_MAX_BLOCK_COUNT_PER_OP;
	while (block_count > 0) {
		bcount = (block_count > async_step)? async_step : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;

		/* Read data from obj. */
		rc = obj_read_async(&obj, fp, obj_io_nr_ops_on_fly,
				    bcount, data);
		if (rc != 0) {
			fprintf(stderr, "Failed in reading from file!\n");
			obj_cleanup_iovecs(data);
			break;
		}

		block_count -= bcount;
		last_index += bcount * block_size;

		++obj_io_nr_ops_on_fly;
	}
	pthread_mutex_unlock(&obj_io_mutex);

	obj_wait_on_all_ops();

	obj_io_async_fini();

obj_close:
	mio_obj_close(&obj);
	fclose(fp);
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
