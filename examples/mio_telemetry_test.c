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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "src/mio_telemetry.h"

/**
 * MIO telemetry tests with log as backend. The tests also
 * show how to use telemetry APIs without Motr or other storage
 * drivers.
 */

void telem_tests()
{
	int i;
	int j;
	char *topic;
	uint64_t val_u64;
	enum mio_telemetry_type type;
	uint16_t val_u16;
	uint32_t val_u32;
	uint64_t *elms_u64;
	struct mio_telemetry_array array;

	topic = malloc(128);
	if (topic == NULL)
		return;
	for (i = 0; i < 1; i++) {
		sprintf(topic, "mio_telemetry_uint64_%d", i);
		type = MIO_TM_TYPE_UINT64;
		val_u64 = (i + 1) * 1024 * 1024;
		mio_telemetry_advertise(topic, type, &val_u64);
	}

	for (i = 0; i < 1; i++) {
		sprintf(topic, "mio_telemetry_uint16_%d", i);
		type = MIO_TM_TYPE_UINT16;
		val_u16 = (i + 1) * 1024;
		mio_telemetry_advertise(topic, type, &val_u16);
	}

	for (i = 0; i < 1; i++) {
		sprintf(topic, "mio_telemetry_uint32_%d", i);
		type = MIO_TM_TYPE_UINT32;
		val_u32 = (i + 1) * 1024 * 128;
		mio_telemetry_advertise(topic, type, &val_u32);
	}

	elms_u64 = malloc(8 * sizeof(uint64_t));
	if (elms_u64 == NULL)
		goto bailout;
	for (i = 0; i < 1; i++) {
		sprintf(topic, "mio_telemetry_array_uint64_%d", i);
		type = MIO_TM_TYPE_ARRAY_UINT64;
		for (j = 0; j < 8; j++)
			elms_u64[j] = (j + 1) * 1024 * 1024;
		array.mta_nr_elms = 8;
		array.mta_elms = elms_u64;
		mio_telemetry_advertise(topic, type, &array);
	}

	mio_telemetry_array_advertise(topic, type,
				      3, elms_u64[0], elms_u64[1], elms_u64[2]);

bailout:
	free(topic);
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_telemetry_conf telem_conf;

	memset(&telem_conf, 0, sizeof telem_conf);
	telem_conf.mtc_prefix = "mio_telemetry_test";
	telem_conf.mtc_type = MIO_TM_ST_LOG;
	telem_conf.mtc_is_parser = false;
	rc = mio_telemetry_init(&telem_conf);
	if (rc < 0) {
		fprintf(stderr, "%s: errno = %d, %s\n",
			"Initialising MIO failed", rc, strerror(-rc));
		exit(EXIT_FAILURE);
	}

	telem_tests();

	mio_telemetry_fini();
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
