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
#include <unistd.h>
#include <asm/byteorder.h>

#include "obj.h"
#include "helpers.h"

static void hstat_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]...\n"
"Create an object.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -o, --object         OID       ID of the Motr object\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

static int obj_hint_stat(struct mio_obj_id *oid)
{
	int i;
	int rc;
	uint64_t hvalue;
	struct mio_obj obj;
	struct mio_hints hints;

	memset(&obj, 0, sizeof obj);
	rc = mio_cmd_obj_open(oid, &obj);
	if (rc < 0)
		return rc;

	memset(&hints, 0, sizeof hints);
	rc = mio_obj_hints_get(&obj, &hints);
	if (rc < 0)
		return rc;

	for (i = 0; i < MIO_HINT_OBJ_KEY_NUM; i++) {
		char *hname = mio_hint_name(MIO_HINT_SCOPE_OBJ, i);
		rc = mio_hint_lookup(&hints, i, &hvalue);
		if (rc < 0)
			continue;

		obj_id_printf(oid);
		fprintf(stderr, "\t%s\t%"PRIu64"\n", hname, hvalue);
	}

	mio_cmd_obj_close(&obj);
	return 0;
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params hstat_params;

	mio_cmd_obj_args_init(argc, argv, &hstat_params, &hstat_usage);

	rc = mio_init(hstat_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = obj_hint_stat(&hstat_params.cop_oid);
	if (rc < 0)
		mio_cmd_error("Listing object's hints failed", rc);

	mio_fini();
	mio_cmd_obj_args_fini(&hstat_params);
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
