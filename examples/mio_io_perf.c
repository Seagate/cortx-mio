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

#include "obj.h"
#include "helpers.h"

enum {
	RWT_MAX_RAND_NUM = 1024
};

struct iob_worker_info {
	int iwi_thrd_idx;

	/* IO arguments. */
	struct mio_obj *iwi_obj;
	uint64_t iwi_offset;
	uint64_t iwi_block_size;
	uint64_t iwi_block_count;

	int iwi_rc;
	struct timeval iwi_io_stime;
	struct timeval iwi_io_etime;
};

static pthread_t **iob_threads = NULL;
static struct iob_worker_info *iob_workers = NULL;
static struct mio_cmd_obj_params iob_params;

static void iob_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]\n"
"MIO IO benchmark.\n"
"\n"
"  -o, --object         OID       Starting object ID\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -c, --block-count    INT       number of blocks written to an object with "\
				 "suffix b/k/m/g/K/M/G\n"
"  -t, --threads                  Number of threads\n"
"  -i, --io-type                  0 for read, 1 for write\n"
"  -a, --async_mode               Set to async IO mode\n"
"  -l, --async_step               Set the number of blocks for each op in async mode\n"
"  -y, --mio_conf                 MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

static void
iob_cal_bw(uint64_t block_size, uint64_t block_count,
	   struct timeval stv, struct timeval etv,
	   double *time_in_sec, double *bw, bool *is_mb)
{
	uint64_t io_size;

	*time_in_sec = (etv.tv_sec - stv.tv_sec) +
		       (etv.tv_usec - stv.tv_usec) / 1000000.0;
	io_size = block_size * block_count;
	*bw = io_size / *time_in_sec;
	if (io_size < 1024 * 1024) {
		*is_mb = false;
		*bw = *bw / 1024;
	} else {
		*is_mb = true;
		*bw = *bw / (1024 * 1024);
	}
}


static void iob_thread_report(struct iob_worker_info *res, bool is_total)
{
	int thrd_idx;
	double time_in_sec;
	double bw;
	bool is_mb;

	thrd_idx = res->iwi_thrd_idx; 
	if (res->iwi_rc < 0 && is_total == false) {
		printf("[Thread %d] IO failed, rc = %d\n",
			thrd_idx, res->iwi_rc);
		return;
	}

	iob_cal_bw(res->iwi_block_size, res->iwi_block_count,
		   res->iwi_io_stime, res->iwi_io_etime,
		   &time_in_sec, &bw, &is_mb);

	if (is_total == true)
		printf("[Total] Time = %f secs, BW = %f %s/s\n",
			time_in_sec, bw, is_mb? "MB" : "KB");
	else
		printf("[Thread %d] Time = %f.6 secs, BW = %f.3 %s/s\n",
			thrd_idx, time_in_sec, bw, is_mb? "MB" : "KB");
}

static void
iob_generate_data(uint64_t bcount, uint64_t bsize,
		  struct mio_iovec *data)
{
	int i;
	int j;
	uint64_t rand;

	for (i = 0; i < bcount; i++) {
		rand = mio_cmd_random(RWT_MAX_RAND_NUM);
		char *ptr = data[i].miov_base;
		for (j = 0; j < bsize/sizeof(rand); j++) {
			memcpy(ptr, &rand, sizeof(rand));
			ptr += sizeof(rand);
		};
	}
}

static int
iob_obj_write(struct mio_obj *obj, uint64_t offset, 
	      uint64_t block_size, uint64_t block_count)
{
	int rc = 0;
	uint64_t last_index;
	uint64_t max_index;
	struct mio_iovec *data;

	last_index = offset;
	max_index = offset + block_size * block_count;
	while (block_count > 0) {
		uint64_t bcount;
		bcount = (block_count > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)?
			  MIO_CMD_MAX_BLOCK_COUNT_PER_OP : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;
		iob_generate_data(bcount, block_size, data);

		rc = obj_write(obj, bcount, data);
		if (rc != 0) {
			fprintf(stderr, "Writing to object failed!\n");
			obj_cleanup_iovecs(data);
			break;
		}

		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}

	return rc;
}

static int
iob_obj_read(struct mio_obj *obj, uint64_t block_size, uint64_t block_count)
{
	int rc = 0;
	uint64_t bcount;
	uint64_t last_index;
	uint64_t max_index;
	struct mio_iovec *data;

	max_index = block_size * block_count;
	last_index = 0;
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
			fprintf(stderr, "Failed in reading from file!\n");
			obj_cleanup_iovecs(data);
			break;
		}

		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}

	return rc;
}

static void* iob_writer(void *in)
{
	int rc;
	struct mio_thread thread;
	struct iob_worker_info *args = (struct iob_worker_info *)in;

	mio_thread_init(&thread);

	gettimeofday(&args->iwi_io_stime, NULL);
	rc = iob_obj_write(args->iwi_obj,
			   args->iwi_offset,
			   args->iwi_block_size,
			   args->iwi_block_count);
	args->iwi_rc = rc;
	gettimeofday(&args->iwi_io_etime, NULL);

	iob_thread_report(args, false);
	mio_thread_fini(&thread);
	return NULL;
}

static void* iob_reader(void *in)
{
	int rc;
	struct mio_thread thread;
	struct iob_worker_info *args = (struct iob_worker_info *)in;

	mio_thread_init(&thread);

	gettimeofday(&args->iwi_io_stime, NULL);
	rc = iob_obj_read(args->iwi_obj,
			  args->iwi_block_size,
			  args->iwi_block_count);
	args->iwi_rc = rc;
	gettimeofday(&args->iwi_io_etime, NULL);

	iob_thread_report(args, false);
	mio_thread_fini(&thread);

	return NULL;
}

enum iob_io_type {
	IOB_READ = 0,
	IOB_WRITE = 1
};

static int
iob_threads_start(int nr_threads, enum iob_io_type iotype, struct mio_obj *obj,
		   uint64_t block_size, uint64_t block_count)
{
	int i;
	int rc = 0;
	uint64_t blk_cnt_left;
	uint64_t blk_cnt_per_thread;
	struct iob_worker_info *args;

	iob_threads = malloc(nr_threads * sizeof(*iob_threads));
	iob_workers = malloc((nr_threads + 1) * sizeof(*iob_workers));
	if (iob_threads == NULL || iob_workers == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	/* The last one is used for aggregate bandwidth for all threads. */
	args = iob_workers + nr_threads;
	args->iwi_block_size = block_size;
	args->iwi_block_count = block_count;
	gettimeofday(&args->iwi_io_stime, NULL);

	blk_cnt_left = block_count;
	blk_cnt_per_thread = (block_count + nr_threads - 1)/ nr_threads;
	for (i = 0; i < nr_threads; i++) {
		args = iob_workers + i;
		args->iwi_thrd_idx = i;
		args->iwi_obj = obj;
		args->iwi_offset = i * blk_cnt_per_thread * block_size;
		args->iwi_block_size = block_size;
		if (blk_cnt_left > blk_cnt_per_thread)
			args->iwi_block_count = blk_cnt_per_thread;
		else
			args->iwi_block_count = blk_cnt_left;

		if (iotype == IOB_WRITE)
			rc = mio_cmd_thread_init(
				iob_threads + i, &iob_writer, iob_workers + i);
		else
			rc = mio_cmd_thread_init(
				iob_threads + i, &iob_reader, iob_workers + i);
		if (rc < 0)
			goto error;

		blk_cnt_left -= args->iwi_block_count;
	}

	return rc;

error:
	if (iob_threads)
		free(iob_threads);
	if (iob_workers)
		free(iob_workers);
	return rc;
}

static void iob_threads_stop(int nr_threads, struct mio_obj *obj)
{
	int i;

	for (i = 0; i < nr_threads; i++) {
		if (iob_threads[i] == NULL)
			break;

		mio_cmd_thread_join(iob_threads[i]);
		mio_cmd_thread_fini(iob_threads[i]);
		iob_threads[i] = NULL;

	}
	gettimeofday(&iob_workers[nr_threads].iwi_io_etime, NULL);
	iob_thread_report(iob_workers + nr_threads, true);

	free(iob_threads);
	free(iob_workers);
	mio_obj_close(obj);
	return;
}

static int iob_io_by_threads()
{
	int rc;
	enum iob_io_type iotype;
	struct mio_obj *obj;

	obj = malloc(sizeof *obj);
	if (obj == NULL)
		return -ENOMEM;
	memset(obj, 0, sizeof *obj);
	rc = obj_open_or_create(&iob_params.cop_pool_id,
				&iob_params.cop_oid, obj, NULL);
	if (rc < 0) {
		free(obj);
		return rc;
	}

	if (iob_params.cop_io_type == 0)
		iotype = IOB_READ;
	else
		iotype = IOB_WRITE;

	rc = iob_threads_start(iob_params.cop_nr_threads, iotype,
			       obj, iob_params.cop_block_size,
			       iob_params.cop_block_count);
	if (rc < 0) {
		mio_cmd_error("Failed to start all threads!", rc);
		return rc;
	}

	iob_threads_stop(iob_params.cop_nr_threads, obj);
	return rc;
}

static int iob_io_by_async()
{
	int rc;
	struct timeval stv;
	struct timeval etv;
	double time_in_sec;
	double bw;
	bool is_mb;

	gettimeofday(&stv, NULL);
	if (iob_params.cop_io_type == 0)
		rc = mio_cmd_obj_read_async(&iob_params.cop_oid, NULL,
					    iob_params.cop_block_size,
					    iob_params.cop_block_count,
					    iob_params.cop_async_step);
	else
		rc = mio_cmd_obj_write_async(NULL, &iob_params.cop_pool_id,
					     &iob_params.cop_oid,
					     iob_params.cop_block_size,
					     iob_params.cop_block_count,
					     iob_params.cop_async_step);
	if (rc < 0)
		return rc;
	gettimeofday(&etv, NULL);

	iob_cal_bw(iob_params.cop_block_size, iob_params.cop_block_count,
		   stv, etv, &time_in_sec, &bw, &is_mb);
	printf("[Async %s] Time = %f secs, BW = %f %s/s\n",
	       iob_params.cop_io_type == 0? "READ" : "WRITE",
	       time_in_sec, bw, is_mb? "MB" : "KB");

	return 0;
}

int main(int argc, char **argv)
{
	int rc;

	mio_cmd_obj_args_init(argc, argv, &iob_params, &iob_usage);

	rc = mio_init(iob_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		goto exit;
	}

	rc = iob_params.cop_async_mode?
	     iob_io_by_async() :
	     iob_io_by_threads();
	if (rc < 0)
		mio_cmd_error("Writing object failed", rc);


exit:
	mio_fini();
	mio_cmd_obj_args_fini(&iob_params);
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
