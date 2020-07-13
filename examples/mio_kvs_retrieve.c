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

#include "kvs.h"
#include "helpers.h"

static void get_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]...\n"
"Insert pairs to a key/value set.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -k, --kvset         ID        ID of the mero object\n"
"  -s, --startkey      No.       The first serial number of keys\n"
"  -n, --pairs         nr_pairs  The number of key/value pairs to insert\n"
"  -y, --mio_conf_file conf      MIO YAML configuration file\n"
"  -l, --log           log       Log file to record key/value pairs\n"
"  -h, --help                    shows this help text and exit\n"
, prog_name);
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_kvs_params get_params;
	FILE *log = NULL;

	mio_cmd_kvs_args_init(argc, argv, &get_params, &get_usage);
	if (get_params.ckp_log != NULL) {
		log = fopen(get_params.ckp_log, "w");
		if (log == NULL)
			exit(EXIT_FAILURE);
	}

	rc = mio_init(get_params.ckp_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = mio_cmd_kvs_retrieve_pairs(&get_params.ckp_kid,
					get_params.ckp_start_kno,
					get_params.ckp_nr_pairs, log);
	if (rc < 0)
		mio_cmd_error("Retrieving key-value pairs failed", rc);

	mio_fini();
	if(log != NULL)
		fclose(log);
	mio_cmd_kvs_args_fini(&get_params);
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
