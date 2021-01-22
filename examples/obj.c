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
#include <asm/byteorder.h>
#include <pthread.h>

#include "obj.h"
#include "helpers.h"

bool print_on_console = false;

int obj_alloc_iovecs(struct mio_iovec **data, uint32_t bcount,
		     uint32_t bsize, uint64_t offset, uint64_t max_offset)
{
	int i;
	int rc;
	struct mio_iovec *iovecs;
	char *base;

	iovecs = mio_mem_alloc(bcount * sizeof(*iovecs));
	base = mio_mem_alloc(bcount * bsize);
	if (iovecs == NULL || base == NULL) {
		fprintf(stderr, "Can't allocate memory!\n");
		rc = -ENOMEM;
		goto error_exit;
	}

	for(i = 0; i < bcount; i++) {
		iovecs[i].miov_base = base + i * bsize;
		iovecs[i].miov_off = offset + i * bsize;
		if (iovecs[i].miov_off + bsize > max_offset)
			iovecs[i].miov_len = max_offset - iovecs[i].miov_off;
		else
			iovecs[i].miov_len = bsize;
	}
	*data = iovecs;
	return 0;

error_exit:
	mio_mem_free(base);
	mio_mem_free(iovecs);
	return rc;
}

void obj_cleanup_iovecs(struct mio_iovec *data)
{
	mio_mem_free(data[0].miov_base);
	mio_mem_free(data);
}

int obj_read_data_from_file(FILE *fp, uint32_t bcount, uint32_t bsize,
			    struct mio_iovec *data)
{
	int i;
	int rc;
	char signature[5] = {'M', 'I', 'O', 'W', 'R'};

	/* Generate pseudo data. */
	if (fp == NULL) {
		for (i = 0; i < bcount; i++)
			memset(data[i].miov_base, signature[i%5],
			       data[i].miov_len);
		return i;
	}

	/* Read from file. */
	for (i = 0; i < bcount; i++) {
		rc = fread(data[i].miov_base, data[i].miov_len, 1, fp);
		if (rc != 1)
			break;
	}
	return i;
}

int obj_write_data_to_file(FILE *fp, uint32_t bcount, struct mio_iovec *data)
{
	int i = 0;
	int j;
	int rc;

	if (fp != NULL) {
		fseek(fp, data[0].miov_off, SEEK_SET);
		for(i = 0; i < bcount; i++) {
			rc = fwrite(data[i].miov_base, data[i].miov_len, 1, fp);
			if (rc != 1) {
				fprintf(stderr,
					"[obj_write_data_to_file] "
					"Writing to output file failed!\n");
				break;
			}
		}
	}

	if (print_on_console) {
		/* putchar the output */
		for (i = 0; i < bcount; ++i) {
			for (j = 0; j < data[i].miov_len; ++j)
				putchar(data[i].miov_base[j]);
		}

	}

	return i;
}

static int pool_id_sscanf(char *idstr, struct mio_pool_id *pool_id)
{
	int rc;
	int n;
	uint64_t u1;
	uint64_t u2;

	if (idstr == NULL || pool_id == NULL)
		return -EINVAL;

	rc = sscanf(idstr, "%"SCNx64" : %"SCNx64" %n", &u1, &u2, &n);
	if (rc < 0)
		return rc;
	pool_id->mpi_hi = u1;	
	pool_id->mpi_lo = u2;	
	return 0;
}

static int obj_id_sscanf(char *idstr, struct mio_obj_id *oid)
{
	int rc;
	int n;
	uint64_t u1;
	uint64_t u2;

	rc = sscanf(idstr, "%"SCNx64" : %"SCNx64" %n", &u1, &u2, &n);
	if (rc < 0)
		return rc;
	u1 = __cpu_to_be64(u1);
	u2 = __cpu_to_be64(u2);

	memcpy(oid->moi_bytes, &u1, sizeof u1);
	memcpy(oid->moi_bytes + sizeof u1, &u2, sizeof u2);
	return 0;
}

void obj_id_printf(struct mio_obj_id *oid)
{
	uint64_t u1;
	uint64_t u2;

	memcpy(&u1, oid->moi_bytes,sizeof u1);
	memcpy(&u2, oid->moi_bytes + sizeof u1, sizeof u2);
	u1 = __be64_to_cpu(u1);
	u2 = __be64_to_cpu(u2);
	fprintf(stderr, "%"PRIx64":%"PRIx64"", u1, u2);
}

int obj_open(struct mio_obj_id *oid, struct mio_obj *obj)
{
	int rc;
	struct mio_op op;

	memset(&op, 0, sizeof op);
	rc = mio_obj_open(oid, obj, &op);
	if (rc != 0)
		return rc;

	/* If the object doesn't exist, -ENOENT will be returned? */
	rc = mio_cmd_wait_on_op(&op);
	return rc;
}

void obj_close(struct mio_obj *obj)
{
	mio_obj_close(obj);
}

int obj_create(struct mio_pool_id *pool, struct mio_obj_id *oid,
	       struct mio_obj *obj, struct mio_cmd_obj_hint *chint)
{
	int rc;
	struct mio_op op;
	struct mio_hints hints;
	struct mio_hints *hints_ptr = NULL;

	rc = obj_open(oid, obj);
	if (rc == 0) {
		fprintf(stderr, "Object exists!\n");
		return -EEXIST;
	} else if (rc == -ENOENT)
		goto create;
	else
		return rc;

create:
	if (chint != NULL) {
		memset(&hints, 0, sizeof hints);
        	mio_hints_init(&hints);
        	rc = mio_hint_add(&hints, chint->co_hkey, chint->co_hvalue);
       		if (rc < 0) {
                	fprintf(stderr, "Failed to set hint %s\n",
                        	mio_hint_name(MIO_HINT_SCOPE_OBJ,
					      chint->co_hkey));
                	return rc;
        	}
		hints_ptr = &hints;
	}

	memset(&op, 0, sizeof op);
	rc = mio_obj_create(oid, pool, hints_ptr, obj, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	return rc;
}

/*
 * Try to open an object first. If it doesn't exist, create
 * a new one.
 */
int obj_open_or_create(struct mio_pool_id *pool, struct mio_obj_id *oid,
		       struct mio_obj *obj, struct mio_cmd_obj_hint *chint)
{
	int rc;
	struct mio_op op;
	struct mio_hints hints;
	struct mio_hints *hints_ptr = NULL;

	rc = obj_open(oid, obj);
	if (rc == 0)
		return 0;
	else if (rc == -ENOENT)
		goto create;
	else
		return rc;

create:
	if (chint != NULL) {
		memset(&hints, 0, sizeof hints);
     		mio_hints_init(&hints);
        	rc = mio_hint_add(&hints, chint->co_hkey, chint->co_hvalue);
        	if (rc < 0) {
                	fprintf(stderr, "Failed to set hint %s\n",
                        	mio_hint_name(MIO_HINT_SCOPE_OBJ,
					      chint->co_hkey));
                	return rc;
        	}
		hints_ptr = &hints;
	}

	memset(&op, 0, sizeof op);
	rc = mio_obj_create(oid, pool, hints_ptr, obj, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	return rc;
}

int obj_rm(struct mio_obj_id *oid)
{
	int rc;
	struct mio_op op;

	memset(&op, 0, sizeof op);
	rc = mio_obj_delete(oid, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	return rc;
}

int mio_cmd_obj_touch(struct mio_obj_id *oid)
{
	struct mio_obj obj;

	memset(&obj, 0, sizeof obj);
	return obj_create(NULL, oid, &obj, NULL);
}

int mio_cmd_obj_unlink(struct mio_obj_id *oid)
{
	return obj_rm(oid);
}

int mio_cmd_obj_open(struct mio_obj_id *oid, struct mio_obj *obj)
{
	return obj_open(oid, obj);
}

void mio_cmd_obj_close(struct mio_obj *obj)
{
	obj_close(obj);
}

int mio_cmd_obj_args_init(int argc, char **argv,
			  struct mio_cmd_obj_params *params,
			  void (*usage)(FILE *, char *))
{
	int v;
	int rc;
	int option_index = 0;
	static struct option l_opts[] = {
				{"pool",        required_argument, NULL, 'p'},
				{"object",      required_argument, NULL, 'o'},
				{"block-size",  required_argument, NULL, 's'},
				{"block-count", required_argument, NULL, 'c'},
				{"nr_objs",     required_argument, NULL, 'n'},
				{"io_type",     required_argument, NULL, 'i'},
				{"async_len",   required_argument, NULL, 'l'},
				{"async_mod",   no_argument,       NULL, 'a'},
				{"console",     no_argument,       NULL, 'v'},
				{"mio_conf",    required_argument, NULL, 'y'},
				{"help",        no_argument,       NULL, 'h'},
				{0,             0,                 0,     0 }};

	memset(params, 0, sizeof *params);
	params->cop_nr_objs = 1;
	params->cop_block_size = 4096;
	params->cop_block_count = ~0ULL;
	params->cop_async_mode = false;
	params->cop_async_step = MIO_CMD_MAX_BLOCK_COUNT_PER_OP;

	while ((v = getopt_long(argc, argv, ":p:o:s:c:n:i:l:y:t:avh", l_opts,
				&option_index)) != -1)
	{
		switch (v) {
		case 'p':
			pool_id_sscanf(optarg, &params->cop_pool_id);
			continue;
		case 'o':
			obj_id_sscanf(optarg, &params->cop_oid);
			continue;
		case 's':
			rc = mio_cmd_strtou64(optarg, &params->cop_block_size);
			if (rc < 0)
				exit(EXIT_FAILURE);
			continue;
		case 'c':
			rc = mio_cmd_strtou64(optarg, &params->cop_block_count);
			if (rc < 0)
				exit(EXIT_FAILURE);
			continue;
		case 'n':
			params->cop_nr_objs = atoi(optarg);
			continue;
		case 'i':
			params->cop_io_type = atoi(optarg);
			continue;
		case 'l':
			rc = mio_cmd_strtou64(optarg, &params->cop_async_step);
			if (rc < 0)
				exit(EXIT_FAILURE);
			continue;
		case 't':
			params->cop_nr_threads = atoi(optarg);
			continue;
		case 'y':
			params->cop_conf_fname = strdup(optarg);
			if (params->cop_conf_fname == NULL)
				exit(EXIT_FAILURE);
			continue;
		case 'a':
			params->cop_async_mode = true;
			continue;
		case 'v':
			print_on_console = true;
			continue;
		case 'h':
			usage(stderr, basename(argv[0]));
			exit(EXIT_FAILURE);
		case '?':
			fprintf(stderr, "Unsupported option '%c'\n",
				optopt);
			usage(stderr, basename(argv[0]));
			exit(EXIT_FAILURE);
		case ':':
			fprintf(stderr, "No argument given for '%c'\n",
				optopt);
			usage(stderr, basename(argv[0]));
			exit(EXIT_FAILURE);
		default:
			fprintf(stderr, "Unsupported option '%c'\n", v);
		}
	}

	return 0;
}

void mio_cmd_obj_args_fini(struct mio_cmd_obj_params *params)
{
	if (params->cop_conf_fname)
		free(params->cop_conf_fname);
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
