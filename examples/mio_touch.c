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

static void touch_usage(FILE *file, char *prog_name)
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

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params touch_params;

	mio_cmd_obj_args_init(argc, argv, &touch_params, &touch_usage);

	rc = mio_init(touch_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = mio_cmd_obj_touch(&touch_params.cop_oid);
	if (rc < 0)
		mio_cmd_error("Creating an object failed", rc);

	mio_fini();
	mio_cmd_obj_args_fini(&touch_params);
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
