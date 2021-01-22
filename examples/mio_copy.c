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

static void copy_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]... SOURCE\n"
"Copy SOURCE to MIO.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -o, --object         OID       ID of the Motr object\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -c, --block-count    INT       number of blocks to copy, can give with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -p, --pool           POOL_ID   Specify which pool the object is created\n"
"  -a, --async_mode               Set to async IO mode\n"
"  -l, --async_step               Set the number of blocks for each op in async mode\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params copy_params;
	char *src_fname = NULL;
        struct timeval stv;
	struct timeval etv;
	uint64_t time;
	double time_in_sec;
	double bw;

	mio_cmd_obj_args_init(argc, argv, &copy_params, &copy_usage);
	if (argv[optind] != NULL)
		src_fname = strdup(argv[optind]);

	gettimeofday(&stv, NULL);
	rc = mio_init(copy_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}
	gettimeofday(&etv, NULL);
	time = (etv.tv_sec - stv.tv_sec) * 1000000 + etv.tv_usec - stv.tv_usec; 
	printf("MIO Initilisation Time: %lu.%lu secs\n",
		time / 1000000, time % 1000000);

	gettimeofday(&stv, NULL);
	rc = copy_params.cop_async_mode?
	     mio_cmd_obj_write_async(src_fname,
				     &copy_params.cop_pool_id,
				     &copy_params.cop_oid,
				     copy_params.cop_block_size,
				     copy_params.cop_block_count,
				     copy_params.cop_async_step) :
	     mio_cmd_obj_write(src_fname,
			       &copy_params.cop_pool_id,
			       &copy_params.cop_oid,
			       copy_params.cop_block_size,
			       copy_params.cop_block_count);
	if (rc < 0)
		mio_cmd_error("Writing object failed", rc);
	gettimeofday(&etv, NULL);
	time = (etv.tv_sec - stv.tv_sec) * 1000000 + etv.tv_usec - stv.tv_usec; 
	time_in_sec = time / 1000000.0;
	bw = copy_params.cop_block_size * copy_params.cop_block_count / (1024 * 1024);
	bw = bw / time_in_sec;
	printf("MIO WRITE: Time = %lu.%lu secs, BW = %f.3 MB/s\n",
		time / 1000000, time % 1000000, bw);


	gettimeofday(&stv, NULL);
	mio_fini();
	gettimeofday(&etv, NULL);
	time = (etv.tv_sec - stv.tv_sec) * 1000000 + etv.tv_usec - stv.tv_usec; 
	printf("MIO Finalisation Time: %lu.%lu secs\n",
		time / 1000000, time % 1000000);

	free(src_fname);
	mio_cmd_obj_args_fini(&copy_params);
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
