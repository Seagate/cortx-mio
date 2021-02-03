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

static void cat_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]... OUTPUT_FILE\n"
"DISPLAY an MIO object's content to OUTPUT_FILE.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -o, --object         OID       ID of the Motr object\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -c, --block-count    INT       number of blocks for IO, can give with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -a, --async_mode               Set to async IO mode\n"
"  -l, --async_step               Set the number of blocks for each op in async mode\n"
"  -v, --print_on_console         print content on console\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params cat_params;
	char *dst_fname = NULL;

	mio_cmd_obj_args_init(argc, argv, &cat_params, &cat_usage);
	if (argv[optind] != NULL)
		dst_fname = strdup(argv[optind]);

	/*
	 * For async mode, output file has to be provided as the object's
	 * content is read from object and printed on console out of order.
	 */
	if ((cat_params.cop_async_mode == true && dst_fname == NULL) ||
	    (cat_params.cop_async_mode == false &&
             print_on_console == false && dst_fname == NULL)) {
		fprintf(stderr, "Missed source file to write to !\n");
		cat_usage(stderr, basename(argv[0]));
		exit(-1);
	}

	rc = mio_init(cat_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = cat_params.cop_async_mode?
	     mio_cmd_obj_read_async(&cat_params.cop_oid, dst_fname,
				    cat_params.cop_block_size,
				    cat_params.cop_block_count,
				    cat_params.cop_async_step) :
	     mio_cmd_obj_read(&cat_params.cop_oid, dst_fname,
			      cat_params.cop_block_size,
			      cat_params.cop_block_count);
	if (rc < 0)
		mio_cmd_error("Reading object failed", rc);

	mio_fini();
	if (dst_fname)
		free(dst_fname);
	mio_cmd_obj_args_fini(&cat_params);
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
