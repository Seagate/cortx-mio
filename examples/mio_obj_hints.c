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
#include <asm/byteorder.h>

#include "obj.h"
#include "helpers.h"

static char *src_fname = NULL;
static struct mio_cmd_obj_params obj_hint_params;

static void obj_hint_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]... SOURCE\n"
"Copy SOURCE to MIO.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -o, --object         OID       ID of the mero object\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -c, --block-count    INT       number of blocks to copy, can give with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -a, --async_mode               Set to async IO mode\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

static void
obj_id_set(struct mio_obj_id *orig_oid, struct mio_obj_id *new_oid, int idx)
{
	mio_cmd_obj_id_clone(orig_oid, new_oid, idx, 0);
}

static int obj_hint_stat(struct mio_obj_id *oid, int hkey)
{
	int rc;
	uint64_t hvalue;
	struct mio_obj obj;

	memset(&obj, 0, sizeof obj);
	rc = mio_cmd_obj_open(oid, &obj);
	if (rc < 0)
		return rc;

	rc = mio_obj_hint_get(&obj, hkey, &hvalue);
	if (rc < 0)
		fprintf(stderr, "Can't get %s's value, rc = %d\n",
			mio_hint_name(MIO_HINT_SCOPE_OBJ, hkey), rc);
	
	mio_cmd_obj_close(&obj);
	return rc;
}

static int
obj_create_by_hint(struct mio_obj_id *oid, int hkey, uint64_t hvalue)
{
	int rc;
	struct mio_obj obj;
	struct mio_op op;
	struct mio_hints hints;

	memset(&op, 0, sizeof op);
	rc = obj_open(oid, &obj);
	if (rc == 0) {
		fprintf(stderr, "Object exists!\n");
		return -EEXIST;
	} else if (rc == -ENOENT)
		goto create;
	else
		return rc;

create:
	mio_hints_init(&hints);
	rc = mio_hint_add(&hints, hkey, hvalue);
	if (rc < 0) {
		fprintf(stderr, "Failed to set hint %s\n",
			mio_hint_name(MIO_HINT_SCOPE_OBJ, hkey));
		return rc;
	}

	memset(&op, 0, sizeof op);
	rc = mio_obj_create(oid, NULL, &hints, &obj, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	return rc;
}


static int obj_pool_select_by_perf(struct mio_obj_id *oid)
{
	int rc;

	rc = obj_create_by_hint(oid, MIO_HINT_OBJ_WHERE, MIO_POOL_GOLD);
	if (rc < 0)
		mio_cmd_error("Failed to create an object using \
			       MIO_HINT_OBJ_WHERE\n", rc);
	return rc;
}

static int obj_pool_select_by_hotness(struct mio_obj_id *oid)
{
	int rc;
	uint64_t hotness;

	hotness = 128;
	rc = obj_create_by_hint(oid, MIO_HINT_OBJ_HOT_INDEX, hotness);
	if (rc < 0)
		mio_cmd_error("Failed to create an object using \
			       MIO_HINT_OBJ_HOT_INDEX\n", rc);
	return rc;

}

static int obj_hotness(struct mio_obj_id *oid)
{
	int i;
	int rc;

	/* 1. Create and write a new object. */
	rc = mio_cmd_obj_write(src_fname, NULL, oid,
			       obj_hint_params.cop_block_size,
			       obj_hint_params.cop_block_count);
	if (rc < 0) {
		mio_cmd_error("Writing object failed", rc);
		return rc;
	}

	/* Check the object's hotness. */
	rc = obj_hint_stat(&obj_hint_params.cop_oid, MIO_HINT_OBJ_HOT_INDEX);
	if (rc < 0) {
		mio_cmd_error("Writing object failed", rc);
		return rc;
	}

	/* 2. Simulate a workload of intensive WRITE. */
	for (i = 0; i < 16; i++) {
		rc = mio_cmd_obj_write(src_fname, NULL,
				       &obj_hint_params.cop_oid,
				       obj_hint_params.cop_block_size,
				       obj_hint_params.cop_block_count);
		if (rc < 0)
			break;
	}
	if (rc < 0) {
		mio_cmd_error("Writing object failed", rc);
		return rc;
	}

	/* Check the object's hotness again. */
	rc = obj_hint_stat(&obj_hint_params.cop_oid, MIO_HINT_OBJ_HOT_INDEX);
	if (rc < 0) {
		mio_cmd_error("Writing object failed", rc);
		return rc;
	}

	return 0;
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_obj_id oid;

	mio_cmd_obj_args_init(argc, argv, &obj_hint_params, &obj_hint_usage);
	if (argv[optind] != NULL)
		src_fname = strdup(argv[optind]);
	if (src_fname == NULL) {
		fprintf(stderr, "Missed source file to copy !\n");
		obj_hint_usage(stderr, basename(argv[0]));
		exit(-1);
	}

	rc = mio_init(obj_hint_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = obj_hotness(&obj_hint_params.cop_oid);
	if (rc < 0) {
		mio_cmd_error("Failed in object hotness tests!\n", rc);
		goto exit;	
	}

	obj_id_set(&obj_hint_params.cop_oid, &oid, 1);
	rc = obj_pool_select_by_perf(&oid);
	if (rc < 0)
		goto exit;	

	obj_id_set(&obj_hint_params.cop_oid, &oid, 2);
	rc = obj_pool_select_by_hotness(&oid);
	if (rc < 0)
		goto exit;	

exit:
	mio_fini();
	free(src_fname);
	mio_cmd_obj_args_fini(&obj_hint_params);
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
