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

static void hset_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]...\n"
"Create an object.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -o, --object         OID       ID of the mero object\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

static int obj_hint_set(struct mio_obj_id *oid)
{
	int i;
	int rc;
	struct mio_obj obj;
	struct mio_hints hints;

	memset(&obj, 0, sizeof obj);
	rc = mio_cmd_obj_open(oid, &obj);
	if (rc < 0)
		return rc;
	mio_hints_init(&hints);

	for (i = 0; i < MIO_HINT_KEY_NUM; i++) {
		rc = mio_hint_add(&hints, i, i);
		if (rc < 0) {
			fprintf(stderr, "Failed to set hint %s\n",
				mio_hint_name(i));
			break;
		}
	}
	mio_obj_hints_set(&obj, &hints);

	mio_hints_fini(&hints);
	mio_cmd_obj_close(&obj);
	return 0;
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params hset_params;

	mio_cmd_obj_args_init(argc, argv, &hset_params, &hset_usage);

	rc = mio_init(hset_params.cop_conf_fname);
	if (rc < 0) {
		fprintf(stderr, "mio_init failed! rc = %d\n", rc);
		exit(EXIT_FAILURE);
	}

	rc = obj_hint_set(&hset_params.cop_oid);
	if (rc < 0)
		fprintf(stderr, "mio_cmd_obj_hset failed! rc = %d\n", rc);

	mio_fini();
	mio_cmd_obj_args_fini(&hset_params);
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
