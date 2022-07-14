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

#include "obj.h"
#include "helpers.h"

enum {
	MIO_POOL_ID_NULL = 0
};

static void pool_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]...\n"
"Create an object.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -p, --pool         PID         Pool ID \n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

static bool is_pool_id_set(struct mio_pool_id *pool_id)
{
	if (pool_id->mpi_hi == MIO_POOL_ID_NULL &&
	    pool_id->mpi_lo == MIO_POOL_ID_NULL)
		return false;
	else
		return true;
}

static void pool_print(int nr_pools, struct mio_pool *pools)
{
	int i;
	int j;
	char *tier;

	for (i = 0; i < nr_pools; i++) {
		struct mio_pool *pool = pools + i;
		if (pool->mp_type == MIO_POOL_TYPE_SSD)
			tier = "SSD";
		else if (pool->mp_type == MIO_POOL_TYPE_HDD)
			tier = "HDD";
		else if (pool->mp_type == MIO_POOL_TYPE_NVM)
			tier = "NVM";
		else
			tier = "UNKNOW";
		
		fprintf(stderr, "[%"PRIx64":%"PRIx64"]\n",
			pool->mp_id.mpi_hi, pool->mp_id.mpi_lo);
		fprintf(stderr, "Tier: %s\n", tier);
		fprintf(stderr, "Optimised data buffer alignment: %ld\n",
			pool->mp_opt_alignment);

		for (j = 0; j < pool->mp_nr_opt_blksizes; j++) {
			fprintf(stderr, "[%d]Optimised IO block size: %ld\n",
				j, pool->mp_opt_blksizes[j]);
		}
	}
}

static int pool_get(struct mio_pool_id *pool_id)
{
	int rc;
	struct mio_pool *pool;
	struct mio_pools *pools;

	if (is_pool_id_set(pool_id)) {
		rc = mio_pool_get(pool_id, &pool);
		if (rc < 0)
			fprintf(stderr,
				"Failed to query pool %"PRIx64":%"PRIx64"",
				pool_id->mpi_hi, pool_id->mpi_lo);
		else {
			pool_print(1, pool);
			free(pool);
		}
	} else {
		rc = mio_pool_get_all(&pools);
		if (rc < 0)
			fprintf(stderr, "Failed to query pools!\n");
		else {
			pool_print(pools->mps_nr_pools, pools->mps_pools);
			free(pools);
		}
	}

	return rc;
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params pool_params;

	mio_cmd_obj_args_init(argc, argv, &pool_params, &pool_usage);
	rc = mio_init(pool_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = pool_get(&pool_params.cop_pool_id);

	mio_fini();
	mio_cmd_obj_args_fini(&pool_params);
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
