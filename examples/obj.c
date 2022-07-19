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

bool print_on_console = false;

int obj_alloc_iovecs(struct mio_iovec **data, uint64_t bcount,
		     uint64_t bsize, uint64_t offset, uint64_t max_offset)
{
	int i;
	int rc;
	struct mio_iovec *iovecs;
	char *base;

	iovecs = malloc(bcount * sizeof(*iovecs));
	base = malloc(bcount * bsize);
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
	free(base);
	free(iovecs);
	return rc;
}

void obj_cleanup_iovecs(struct mio_iovec *data)
{
	free(data[0].miov_base);
	free(data);
}

int obj_read_data_from_file(FILE *fp, uint64_t bcount, uint64_t bsize,
			    struct mio_iovec *data)
{
	int i;
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
		if (fread(data[i].miov_base, data[i].miov_len, 1, fp) != 1)
			break;
	}
	return i;
}

int obj_write_data_to_file(FILE *fp, uint64_t bcount, struct mio_iovec *data)
{
	int i = 0;
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
			for (int j = 0; j < data[i].miov_len; ++j)
				putchar(data[i].miov_base[j]);
		}

	}

	return i;
}

int obj_write_init(struct mio_pool_id *pool, struct mio_obj_id *oid,
		   struct mio_obj *obj, char *src,
		   uint64_t block_size, uint64_t *block_count,
		   uint64_t *max_index, uint64_t *max_block_count, FILE **fp)
{
	int rc;
	struct stat src_stat;

	/* If `src` file is not set, use pseudo data. */
	if (src == NULL) {
		*max_index = *block_count * block_size;
		goto create_obj;
	}

	/* Open source file */
	*fp = fopen(src, "r");
	if (*fp == NULL)
		return -errno;

	rc = fstat(fileno(*fp), &src_stat);
	if (rc < 0) {
		fclose(*fp);
		return rc;
	}
	*max_index = src_stat.st_size;
	*max_block_count = (*max_index - 1) / block_size + 1;
	*block_count = *block_count > *max_block_count?
		       *max_block_count : *block_count;

create_obj:
	memset(obj, 0, sizeof *obj);
	rc = obj_open_or_create(pool, oid, obj, NULL);
	if (rc < 0)
		fclose(*fp);
	return rc;
}

int obj_read_init(struct mio_obj_id *oid, struct mio_obj *obj, char *dest,
		  uint64_t block_size, uint64_t *block_count,
		  uint64_t *max_index, uint64_t *max_block_count, FILE **fp)
{
	int rc;

	if (dest != NULL) {
		*fp = fopen(dest, "w");
		if (*fp == NULL)
			return -errno;
	}

	rc = obj_open(oid, obj);
	if (rc < 0) {
		fclose(*fp);
		return rc;
	}
	*max_index = obj->mo_attrs.moa_size;
	*max_block_count = (*max_index - 1) / block_size + 1;
	*block_count = *block_count > *max_block_count?
		       *max_block_count : *block_count;

	return 0;
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
	return mio_cmd_id_sscanf(idstr,
				 (uint64_t *)oid->moi_bytes,
				 (uint64_t *)(oid->moi_bytes +
					      sizeof(uint64_t)));
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

static int
obj_do_create(struct mio_pool_id *pool, struct mio_obj_id *oid,
	      struct mio_obj *obj, struct mio_cmd_obj_hint *chint)
{
	int rc;
	struct mio_op op;
	struct mio_hints hints;
	struct mio_hints *hints_ptr = NULL;

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

int obj_create(struct mio_pool_id *pool, struct mio_obj_id *oid,
	       struct mio_obj *obj, struct mio_cmd_obj_hint *chint)
{
	int rc;

	rc = obj_open(oid, obj);
	if (rc == 0) {
		fprintf(stderr, "Object exists!\n");
		return -EEXIST;
	} else if (rc == -ENOENT)
		return obj_do_create(pool, oid, obj, chint);
	else
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

	rc = obj_open(oid, obj);
	if (rc == -ENOENT)
		return obj_do_create(pool, oid, obj, chint);
	else
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
