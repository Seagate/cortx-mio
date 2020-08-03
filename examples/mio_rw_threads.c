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

struct rwt_obj_todo {
	struct mio_obj_id ot_oid;
	uint32_t ot_block_size;
	uint32_t ot_block_count;

	unsigned char ot_md5sum_write[MD5_DIGEST_LENGTH];
	unsigned char ot_md5sum_read[MD5_DIGEST_LENGTH];
	struct rwt_obj_todo *ot_next;
};

struct rwt_obj_todo_list {
	pthread_cond_t otl_cond;
	pthread_mutex_t otl_mutex;

	int otl_nr_in_list;
	int otl_nr_todo;
	int otl_nr_done;
	int otl_nr_failed;
	int otl_nr_matched;
	struct rwt_obj_todo otl_head;
};

static struct rwt_obj_todo_list obj_to_write_list;
static struct rwt_obj_todo_list obj_to_read_list;

enum {
	RWT_MAX_RAND_NUM = 1024
};

static pthread_t **read_threads = NULL;
static pthread_t **write_threads = NULL;
static int *read_tids = NULL;
static int *write_tids = NULL;

/**
 * rwt - Read Write Threads
 */
static void rw_threads_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]\n"
"Example for current Read/Write threads.\n"
"\n"
"  -o, --object         OID       Starting object ID\n"
"  -n, --nr_objs        INT       The number of objects\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -c, --block-count    INT       number of blocks written to an object with "\
				 "suffix b/k/m/g/K/M/G\n"
"  -t, --threads                  Number of threads\n"
"  -y, --mio_conf                 MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

static void
rwt_generate_data(uint32_t bcount, uint32_t bsize,
		  struct mio_iovec *data, MD5_CTX *md5_ctx)
{
	int i;
	int j;
	uint32_t rand;
	char *ptr;

	for (i = 0; i < bcount; i++) {
		rand = mio_cmd_random(RWT_MAX_RAND_NUM);
		ptr = data[i].miov_base;
		for (j = 0; j < bsize/sizeof(rand); j++) {
			memcpy(ptr, &rand, sizeof(rand));
			ptr += sizeof(rand);
		};
		MD5_Update(md5_ctx, data[i].miov_base, bsize);
	}
}

static bool rwt_obj_verify_md5sums(struct rwt_obj_todo *todo)
{
	int i;
	int rc;

	obj_id_printf(&todo->ot_oid);
	printf("\t");
	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
		printf("%02x", todo->ot_md5sum_write[i]);
	printf("\t");
	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
		printf("%02x", todo->ot_md5sum_read[i]);
	printf("\n");

	rc = memcmp(todo->ot_md5sum_write,
		    todo->ot_md5sum_read,
		    MD5_DIGEST_LENGTH);

	return rc == 0? true : false;
}

static int rwt_obj_write(struct rwt_obj_todo *todo)
{
	int rc = 0;
	uint32_t bcount;
	uint32_t block_size;
	uint32_t block_count;
	uint64_t last_index;
	uint64_t max_index;
	struct mio_iovec *data;
	struct mio_obj obj;
	struct mio_obj_id *oid = &todo->ot_oid;
	MD5_CTX md5_ctx;

	block_size = todo->ot_block_size;
	block_count = todo->ot_block_count;
	max_index = block_size * block_count;

	/* Create the target object if it doesn't exist. */
	memset(&obj, 0, sizeof obj);
	rc = obj_create(oid, &obj);
	if (rc < 0)
		return rc;

	last_index = 0;
	MD5_Init(&md5_ctx);
	while (block_count > 0) {
		bcount = (block_count > MIO_CMD_MAX_BLOCK_COUNT_PER_OP)?
			  MIO_CMD_MAX_BLOCK_COUNT_PER_OP : block_count;
		rc = obj_alloc_iovecs(&data, bcount, block_size,
				      last_index, max_index);
		if (rc != 0)
			break;
		rwt_generate_data(bcount, block_size, data, &md5_ctx);

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
	MD5_Final(todo->ot_md5sum_write, &md5_ctx);

	mio_obj_close(&obj);
	return rc;
}

static int rwt_obj_read(struct rwt_obj_todo *todo)
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
	struct mio_obj_id *oid = &todo->ot_oid;
	MD5_CTX md5_ctx;

	block_size = todo->ot_block_size;
	block_count = todo->ot_block_count;
	max_index = block_size * block_count;

	memset(&obj, 0, sizeof obj);
	rc = obj_open(oid, &obj);
	if (rc < 0)
		goto dest_close;

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
			fprintf(stderr, "Failed in reading from file!\n");
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
	MD5_Final(todo->ot_md5sum_read, &md5_ctx);

	mio_obj_close(&obj);

dest_close:
	return rc;
}


static void* rwt_writer(void *in)
{
	int rc;
	struct rwt_obj_todo *todo;
	struct mio_thread thread;

	mio_thread_init(&thread);

	/* main loop */
	while(1) {
		pthread_mutex_lock(&obj_to_write_list.otl_mutex);
		if (obj_to_write_list.otl_nr_in_list == 0) {
			pthread_mutex_unlock(&obj_to_write_list.otl_mutex);
			break;
		}
		todo = obj_to_write_list.otl_head.ot_next;
		assert(todo != NULL);
		obj_to_write_list.otl_head.ot_next = todo->ot_next;
		todo->ot_next = NULL;
		obj_to_write_list.otl_nr_in_list--;
		pthread_mutex_unlock(&obj_to_write_list.otl_mutex);

		rc = rwt_obj_write(todo);

		/* WRITE complete, add to the READ todo list. */
		pthread_mutex_lock(&obj_to_read_list.otl_mutex);
		if (rc == 0) {
			todo->ot_next = obj_to_read_list.otl_head.ot_next;
			obj_to_read_list.otl_head.ot_next = todo;
			obj_to_read_list.otl_nr_in_list++;
		} else if (rc < 0) {
			mio_cmd_error("Failed to write to object", rc);
			obj_to_read_list.otl_nr_failed++;
		}
		pthread_cond_broadcast(&obj_to_read_list.otl_cond);
		pthread_mutex_unlock(&obj_to_read_list.otl_mutex);
	}

	mio_thread_fini(&thread);

	pthread_mutex_lock(&obj_to_read_list.otl_mutex);
	pthread_cond_broadcast(&obj_to_read_list.otl_cond);
	pthread_mutex_unlock(&obj_to_read_list.otl_mutex);

	return NULL;
}

static void* rwt_reader(void *in)
{
	int rc = 0;
	bool matched = false;
	struct rwt_obj_todo *todo;
	struct mio_thread thread;
	int tid = *((int *)in);
	bool read_all_done = false;

	mio_thread_init(&thread);

	if (tid == 1)
		fprintf(stderr, "%10s\t%32s\t%32s\n",
			"Object ID", "WRITE MD5", "READ MD5");

	/* main loop */
	while(1) {
		pthread_mutex_lock(&obj_to_read_list.otl_mutex);
		while (obj_to_read_list.otl_nr_in_list == 0) {
			if ((obj_to_read_list.otl_nr_done +
			     obj_to_read_list.otl_nr_failed) ==
			    obj_to_read_list.otl_nr_todo) {
				read_all_done = true;
				pthread_mutex_unlock(
					&obj_to_read_list.otl_mutex);
				goto exit;
			}

			pthread_cond_wait(&obj_to_read_list.otl_cond,
					  &obj_to_read_list.otl_mutex);
		}
		todo = obj_to_read_list.otl_head.ot_next;
		obj_to_read_list.otl_head.ot_next = todo->ot_next;
		todo->ot_next = NULL;
		obj_to_read_list.otl_nr_in_list--;
		pthread_mutex_unlock(&obj_to_read_list.otl_mutex);

		rc = rwt_obj_read(todo);

		pthread_mutex_lock(&obj_to_read_list.otl_mutex);
		if (rc < 0) {
			mio_cmd_error("Failed to read from object", rc);
			obj_to_read_list.otl_nr_failed++;
		} else {
			matched = rwt_obj_verify_md5sums(todo);
			if (matched)
				obj_to_read_list.otl_nr_matched++;
			obj_to_read_list.otl_nr_done++;
			if ((obj_to_read_list.otl_nr_done +
			     obj_to_read_list.otl_nr_failed) ==
		    	    obj_to_read_list.otl_nr_todo)
				read_all_done = true;
		}
		pthread_mutex_unlock(&obj_to_read_list.otl_mutex);

		obj_rm(&todo->ot_oid);
		free(todo);

exit:
		if (read_all_done)
			break;
	}

	mio_thread_fini(&thread);

	pthread_mutex_lock(&obj_to_read_list.otl_mutex);
	pthread_cond_broadcast(&obj_to_read_list.otl_cond);
	pthread_mutex_unlock(&obj_to_read_list.otl_mutex);

	return NULL;
}

static void
rwt_set_obj_id(struct rwt_obj_todo *todo, struct mio_obj_id *st_oid, int idx)
{
	uint64_t u1;
	uint64_t u2;
	uint64_t n1;
	uint64_t n2;

	memcpy(&u1, st_oid->moi_bytes, sizeof u1);
	memcpy(&u2, st_oid->moi_bytes + sizeof u1, sizeof u2);
	u1 = __be64_to_cpu(u1);
	u2 = __be64_to_cpu(u2);

	n1 = u1 + idx;
	n2 = u2;
	n1 = __cpu_to_be64(n1);
	n2 = __cpu_to_be64(n2);
	memcpy(todo->ot_oid.moi_bytes, &n1, sizeof n1);
	memcpy(todo->ot_oid.moi_bytes + sizeof n1, &n2, sizeof n2);
}

static void rwt_todo_lists_fini()
{
	int i;
	struct rwt_obj_todo *todo;

	pthread_mutex_destroy(&obj_to_read_list.otl_mutex);
	pthread_cond_destroy(&obj_to_read_list.otl_cond);
	pthread_mutex_destroy(&obj_to_write_list.otl_mutex);
	pthread_cond_destroy(&obj_to_write_list.otl_cond);

	for (i = 0; i < obj_to_read_list.otl_nr_in_list; i++) {
		todo = obj_to_read_list.otl_head.ot_next;
		if (todo == NULL)
			break;
		obj_to_read_list.otl_head.ot_next = todo->ot_next;
		free(todo);
	}

	for (i = 0; i < obj_to_write_list.otl_nr_in_list; i++) {
		todo = obj_to_write_list.otl_head.ot_next;
		if (todo == NULL)
			break;
		obj_to_write_list.otl_head.ot_next = todo->ot_next;

		free(todo);
	}
}

static int rwt_todo_lists_init(struct mio_obj_id *st_oid, int nr_objs,
			      uint32_t block_size, uint32_t block_count)
{
	int i;
	int rc = 0;
	struct rwt_obj_todo *todo;

	memset(&obj_to_read_list, 0, sizeof(obj_to_read_list));
	pthread_cond_init(&obj_to_read_list.otl_cond, NULL);
	pthread_mutex_init(&obj_to_read_list.otl_mutex, NULL);
	obj_to_read_list.otl_nr_todo = nr_objs;

	memset(&obj_to_write_list, 0, sizeof(obj_to_write_list));
	pthread_cond_init(&obj_to_write_list.otl_cond, NULL);
	pthread_mutex_init(&obj_to_write_list.otl_mutex, NULL);
	obj_to_write_list.otl_nr_todo = nr_objs;

	/*
 	 * Insert all objects into the WRITE todo list.
 	 */
	for (i = 0; i < nr_objs; i++) {
		todo = malloc(sizeof(struct rwt_obj_todo));
		if (todo == NULL) {
			rc = -ENOMEM;
			break;
		}

		rwt_set_obj_id(todo, st_oid, i);
		todo->ot_block_size = block_size;
		todo->ot_block_count = block_count;
		todo->ot_next = obj_to_write_list.otl_head.ot_next;
		obj_to_write_list.otl_head.ot_next = todo;
		obj_to_write_list.otl_nr_in_list++;
	}
	if (rc < 0)
		rwt_todo_lists_fini();

	return rc;
}
static void rwt_report()
{
	fprintf(stderr, "[Final Report] \t");
	fprintf(stderr, "Objects TODO: %d\t", obj_to_read_list.otl_nr_todo);
	fprintf(stderr, "Completed: %d\t", obj_to_read_list.otl_nr_done);
	fprintf(stderr, "Failed: %d\t", obj_to_read_list.otl_nr_failed);
	fprintf(stderr, "Matched: %d\t", obj_to_read_list.otl_nr_matched);
	fprintf(stderr, "\n");
}

static int
mio_rwt_start(int nr_threads, struct mio_obj_id *st_oid, int nr_objs,
	      uint32_t block_size, uint32_t block_count)
{
	int i;
	int rc = 0;

	rwt_todo_lists_init(st_oid, nr_objs, block_size, block_count);

	/*
	 * For simplicity, the same number of READ and WRITE threads
	 * are created.
	 */
	read_threads = malloc(nr_threads * sizeof(*read_threads));
	write_threads = malloc(nr_threads * sizeof(*write_threads));
	read_tids = malloc(nr_threads * sizeof(*read_tids));
	write_tids = malloc(nr_threads * sizeof(*write_tids));
	if (read_threads == NULL || write_threads == NULL ||
	    read_tids == NULL || write_tids == NULL) {
		rc = -ENOMEM;
		goto error;
	}

	for (i = 0; i < nr_threads; i++) {
		write_tids[i] = i;
		rc = mio_cmd_thread_init(
			write_threads + i, &rwt_writer, write_tids + i);
		if (rc < 0)
			goto error;
	}

	for (i = 0; i < nr_threads; i++) {
		read_tids[i] = i;
		rc = mio_cmd_thread_init(
			read_threads + i, &rwt_reader, read_tids + i);
		if (rc < 0)
			goto error;
	}

	return 0;

error:
	if (read_threads)
		free(read_threads);
	if (write_threads)
		free(write_threads);
	if (read_tids)
		free(read_tids);
	if (write_tids)
		free(write_tids);
	return rc;
}

static void mio_rwt_stop(int nr_threads)
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
	free(read_tids);
	free(write_tids);

	rwt_report();

	rwt_todo_lists_fini();

	return;
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params rwt_params;

	mio_cmd_obj_args_init(argc, argv, &rwt_params, &rw_threads_usage);

	rc = mio_init(rwt_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		goto exit;
	}

	rc = mio_rwt_start(rwt_params.cop_nr_threads, &rwt_params.cop_oid,
			   rwt_params.cop_nr_objs,
			   rwt_params.cop_block_size,
			   rwt_params.cop_block_count);
	if (rc < 0) {
		mio_cmd_error("Failed to start threads", rc);
		goto exit;
	}

	mio_rwt_stop(rwt_params.cop_nr_threads);

exit:
	mio_fini();
	mio_cmd_obj_args_fini(&rwt_params);
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
