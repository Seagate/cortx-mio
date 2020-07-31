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

/**
 * Examples for MIO object lock (exclusive whole object lock).
 *
 * A set of writer and reader threads are created to write/read
 * to/from the same area of an object. A writer writes to the object
 * with the data corresponding to its order of acquiring the object lock.
 * Data read from a reader should match the latest data written by a writer.
 *
 * rwl - read write lock
 */
enum {
        RWL_MAX_RAND_NUM = 1024
};

enum rwl_io_role {
	RWL_WRITER = 0,
	RWL_READER
};

struct rwl_io {
	int io_idx;

	struct mio_obj_id io_oid;
	uint32_t io_block_size;
	uint32_t io_block_count;

	int io_thread_idx;
	enum rwl_io_role io_role;
	unsigned char io_md5sum[MD5_DIGEST_LENGTH];
};

struct rwl_io_report {
	pthread_mutex_t ior_mutex;

	int ior_nr_ioses;  /* Total number of IO jobs. */
	int ior_nr_done; /* how much IO jobs have been done. */
	struct rwl_io *ior_ioses;
};

/* cc - concurrent. */
struct rwl_io_report cc_io_report;

static pthread_t **read_threads = NULL;
static pthread_t **write_threads = NULL;

static void rwl_threads_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]\n"
"Example for current Read/Write threads.\n"
"\n"
"  -o, --object         OID       Starting object ID\n"
"  -n, --nr_objs        INT       The number of objects\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G (see --block-count)\n"
"  -c, --block-count    INT       number of blocks written to an object with "\
				 "suffix b/k/m/g/K/M/G (see --block-size)\n"
"  -t, --threads                  Number of threads\n"
"  -y, --mio_conf                 MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

static void
rwl_generate_data(uint32_t bcount, uint32_t bsize,
		  struct mio_iovec *data, MD5_CTX *md5_ctx)
{
	int i;
	int j;
	uint32_t rand;
	char *ptr;

	for (i = 0; i < bcount; i++) {
		rand = mio_cmd_random(RWL_MAX_RAND_NUM);
		ptr = data[i].miov_base;
		for (j = 0; j < bsize/sizeof(rand); j++) {
			memcpy(ptr, &rand, sizeof(rand));
			ptr += sizeof(rand);
		};
		MD5_Update(md5_ctx, data[i].miov_base, bsize);
	}
}

static void rwl_print_io(struct rwl_io *io)
{
	int i;
	int rc = 0;
	struct rwl_io *last_io = NULL;
	bool pass;

	printf("[%d]\t%s\t", io->io_idx,
	       io->io_role == RWL_WRITER? "WRITER" : "READER");

	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
		printf("%02x", io->io_md5sum[i]);
	printf("\t");

	if (io->io_role == RWL_WRITER) {
		printf("\n");
		return;
	}

	pthread_mutex_lock(&cc_io_report.ior_mutex);
	if (io->io_idx > 0)
		last_io = cc_io_report.ior_ioses + io->io_idx - 1;
	pthread_mutex_unlock(&cc_io_report.ior_mutex);

	if (last_io != NULL) {
		rc = memcmp(io->io_md5sum, last_io->io_md5sum,
			    MD5_DIGEST_LENGTH);
		pass = rc == 0? true : false;
	} else
		pass = true;
	printf("%s\n", pass? "MATCHED" : "FAILED");
}

static int rwl_obj_write(struct mio_obj_id *oid)
{
	int rc = 0;
	uint32_t bcount;
	uint32_t block_size;
	uint32_t block_count;
	uint64_t last_index;
	uint64_t max_index;
	struct mio_iovec *data;
	struct mio_obj obj;
	struct rwl_io *io;
	MD5_CTX md5_ctx;

	memset(&obj, 0, sizeof obj);
	rc = obj_open(oid, &obj);
	if (rc < 0)
		return rc;

	/* Get the object lock. */
	rc = mio_obj_lock(&obj);
	if (rc < 0)
		goto close_obj;

	/* Get the IO task. */
	pthread_mutex_lock(&cc_io_report.ior_mutex);
	io = cc_io_report.ior_ioses + cc_io_report.ior_nr_done;
	io->io_role = RWL_WRITER;
	cc_io_report.ior_nr_done++;
	pthread_mutex_unlock(&cc_io_report.ior_mutex);

	block_size = io->io_block_size;
	block_count = io->io_block_count;
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
		rwl_generate_data(bcount, block_size, data, &md5_ctx);

		rc = obj_write(&obj, bcount, data);
		if (rc != 0) {
			obj_cleanup_iovecs(data);
			break;
		}

		obj_cleanup_iovecs(data);
		block_count -= bcount;
		last_index += bcount * block_size;
	}
	MD5_Final(io->io_md5sum, &md5_ctx);
	rwl_print_io(io);

	mio_obj_unlock(&obj);

close_obj:
	mio_obj_close(&obj);
	return rc;
}

static int rwl_obj_read(struct mio_obj_id *oid)
{
	int i;
	int rc = 0;
	uint32_t bcount;
	uint32_t block_size;
	uint32_t block_count;
	uint64_t last_index;
	uint64_t max_index;
	struct mio_iovec *data;
	struct mio_obj obj;
	struct rwl_io *io;
	MD5_CTX md5_ctx;

	memset(&obj, 0, sizeof obj);
	rc = obj_open(oid, &obj);
	if (rc < 0)
		return rc;

	/* Get the object lock. */
	rc = mio_obj_lock(&obj);
	if (rc < 0)
		goto close_obj;

	pthread_mutex_lock(&cc_io_report.ior_mutex);
	io = cc_io_report.ior_ioses + cc_io_report.ior_nr_done;
	io->io_role = RWL_READER;
	cc_io_report.ior_nr_done++;
	pthread_mutex_unlock(&cc_io_report.ior_mutex);

	block_size = io->io_block_size;
	block_count = io->io_block_count;
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
		rc = obj_read(&obj, bcount, data);
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
	MD5_Final(io->io_md5sum, &md5_ctx);
	rwl_print_io(io);

	mio_obj_unlock(&obj);

close_obj:
	mio_obj_close(&obj);

	return rc;
}


static void* rwl_writer(void *in)
{
	int rc;
	struct mio_thread thread;
	struct mio_obj_id *oid = (struct mio_obj_id *)in;

	mio_thread_init(&thread);

	rc = rwl_obj_write(oid);
	if (rc < 0)
		fprintf(stderr, "Failed to write to object: errno = %d\n", rc);

	mio_thread_fini(&thread);
	return NULL;
}

static void* rwl_reader(void *in)
{
	int rc = 0;
	struct mio_thread thread;
	struct mio_obj_id *oid = (struct mio_obj_id *)in;

	mio_thread_init(&thread);

	rc = rwl_obj_read(oid);
	if (rc < 0)
		fprintf(stderr, "Failed to read from object: errno = %d\n", rc);

	mio_thread_fini(&thread);

	return NULL;
}

static void
rwl_set_obj_id(struct mio_obj_id *to_oid, struct mio_obj_id *from_oid)
{
	memcpy(to_oid->moi_bytes, from_oid->moi_bytes, MIO_OBJ_ID_LEN);
}

static void rwl_io_report_fini()
{
	pthread_mutex_destroy(&cc_io_report.ior_mutex);
	free(cc_io_report.ior_ioses);
}

static int rwl_io_report_init(struct mio_obj_id *oid, int nr_ioses,
			      uint32_t block_size, uint32_t block_count)
{
	int i;
	int rc = 0;
	struct rwl_io *io;

	memset(&cc_io_report, 0, sizeof(cc_io_report));
	pthread_mutex_init(&cc_io_report.ior_mutex, NULL);

	cc_io_report.ior_ioses = malloc(nr_ioses * sizeof(struct rwl_io));
	if (cc_io_report.ior_ioses == NULL)
		return -ENOMEM;

	for (i = 0; i < nr_ioses; i++) {
		io = cc_io_report.ior_ioses + i;
		io->io_idx = i;
		rwl_set_obj_id(&io->io_oid, oid);
		io->io_block_size = block_size;
		io->io_block_count = block_count;
	}

	return rc;
}

static int
mio_rwl_start(int nr_threads, struct mio_obj_id *oid,
	      uint32_t block_size, uint32_t block_count)
{
	int i;
	int rc = 0;

	rwl_io_report_init(oid, 2 * nr_threads + 1, block_size, block_count);

	/* Prepare the test object. */
	rc = rwl_obj_write(oid);
	if (rc < 0) {
		fprintf(stderr, "Failed to write to the object: errno = %d\n", rc);
		return rc;
	}

	/*
	 * For simplicity, the same number of READ and WRITE threads
	 * are created.
	 */
	read_threads = malloc(nr_threads * sizeof(*read_threads));
	write_threads = malloc(nr_threads * sizeof(*write_threads));
	if (read_threads == NULL || write_threads == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	for (i = 0; i < nr_threads; i++) {
		rc = mio_cmd_thread_init(
			write_threads + i, &rwl_writer, oid);
		if (rc < 0)
			goto error;
	}

	for (i = 0; i < nr_threads; i++) {
		rc = mio_cmd_thread_init(
			read_threads + i, &rwl_reader, oid);
		if (rc < 0)
			goto error;
	}

	return 0;

error:
	if (read_threads)
		free(read_threads);
	if (write_threads)
		free(write_threads);
	return rc;
}

static void mio_rwl_stop(int nr_threads)
{
	int i;

	for (i = 0; i < nr_threads; i++) {
		if (write_threads[i] == NULL)
			break;

		mio_cmd_thread_join(write_threads[i]);
		mio_cmd_thread_fini(write_threads[i]);
		write_threads[i] = NULL;
	}

	for (i = 0; i < nr_threads; i++) {
		if (read_threads[i] == NULL)
			break;

		mio_cmd_thread_join(read_threads[i]);
		mio_cmd_thread_fini(read_threads[i]);
		read_threads[i] = NULL;

	}
	free(read_threads);
	free(write_threads);

	rwl_io_report_fini();
	return;
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params rwl_params;

	mio_cmd_obj_args_init(argc, argv, &rwl_params, &rwl_threads_usage);

	rc = mio_init(rwl_params.cop_conf_fname);
	if (rc < 0) {
		fprintf(stderr, "mio_init failed! errno = %d\n", rc);
		goto exit;
	}

	/* Create the target object if it doesn't exist. */
	rc = mio_cmd_obj_touch(&rwl_params.cop_oid);
	if (rc < 0)
		goto exit;

	rc = mio_rwl_start(rwl_params.cop_nr_threads, &rwl_params.cop_oid,
			   rwl_params.cop_block_size,
			   rwl_params.cop_block_count);
	if (rc < 0) {
		fprintf(stderr, "Failed to start threads! errno = %d\n", rc);
		goto exit;
	}

	mio_rwl_stop(rwl_params.cop_nr_threads);

	rc = mio_cmd_obj_unlink(&rwl_params.cop_oid);
exit:
	mio_fini();
	mio_cmd_obj_args_fini(&rwl_params);
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
