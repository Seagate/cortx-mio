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
"  -o, --object         OID       ID of the mero object\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G (see --block-count)\n"
"  -c, --block-count    INT       number of blocks to copy, can give with " \
				 "suffix b/k/m/g/K/M/G (see --block-size)\n"
"  -a, --async_mode               Set to async IO mode\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params copy_params;
	char *src_fname = NULL;

	mio_cmd_obj_args_init(argc, argv, &copy_params, &copy_usage);
	if (argv[optind] != NULL)
		src_fname = strdup(argv[optind]);
	if (src_fname == NULL) {
		fprintf(stderr, "Missed source file to copy !\n");
		copy_usage(stderr, basename(argv[0]));
		exit(-1);
	}

	rc = mio_init(copy_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = copy_params.cop_async_mode?
	     mio_cmd_obj_write_async(src_fname, &copy_params.cop_oid,
				     copy_params.cop_block_size,
				     copy_params.cop_block_count) :
	     mio_cmd_obj_write(src_fname, &copy_params.cop_oid,
			       copy_params.cop_block_size,
			       copy_params.cop_block_count);
	if (rc < 0)
		mio_cmd_error("Writing object failed", rc);

	mio_fini();
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
