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
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#ifdef MIO_MOTR_ADDB
#include "src/mio.h"
#endif
#include "src/mio_telemetry.h"

static void usage()
{
	fprintf(stderr, "Usage: mio_telemetry_parser files [addb | log].\n");
}

enum {
	PARSER_MAX_REC_LEN = 256
};

void print_u16(char *buf, uint16_t value)
{
	buf += strlen(buf);
	sprintf(buf, " %u", value);
}

void print_u32(char *buf, uint32_t value)
{
	buf += strlen(buf);
	sprintf(buf, " %u", value);
}

void print_u64(char *buf, uint64_t value)
{
	buf += strlen(buf);
	sprintf(buf, " %"PRIu64"", value);
}

int print_telemetry_rec_array(char *buf, struct mio_telemetry_rec *rec)
{
	int rc = 0;
	int i;
	int nr_elms;
	void *elms;
	enum mio_telemetry_type type;
	struct mio_telemetry_array *array;

	type = rec->mtr_type;
	array = (struct mio_telemetry_array *)rec->mtr_value;
	nr_elms = array->mta_nr_elms;
	elms = array->mta_elms;

	for (i = 0; i < nr_elms; i++) {
		if (type == MIO_TM_TYPE_ARRAY_UINT16)
			print_u16(buf, ((uint16_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT32)
			print_u32(buf, ((uint32_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT64)
			print_u64(buf, ((uint64_t *)elms)[i]);
		else {
			rc = -EINVAL;
			break;
		}
	}

	return rc;
}

int print_telemetry_rec(struct mio_telemetry_rec *rec)
{
	int rc = 0;
	char *buf;

	buf = malloc(PARSER_MAX_REC_LEN);
	if (buf == NULL)
		return -ENOMEM;

	if (rec->mtr_prefix != NULL)
		sprintf(buf, "%s %s %s",
			rec->mtr_time_str, rec->mtr_prefix, rec->mtr_topic);
	else
		sprintf(buf, "%s %s", rec->mtr_time_str, rec->mtr_topic);

	switch (rec->mtr_type) {
	case MIO_TM_TYPE_UINT16:
		print_u16(buf, *(uint16_t *)rec->mtr_value);
		break;
	case MIO_TM_TYPE_UINT32:
		print_u32(buf, *(uint32_t *)rec->mtr_value);
		break;
	case MIO_TM_TYPE_UINT64:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_TIMEPOINT:
		print_u64(buf, *(uint64_t *)rec->mtr_value);
		break;
	case MIO_TM_TYPE_ARRAY_UINT16:
	case MIO_TM_TYPE_ARRAY_UINT32:
	case MIO_TM_TYPE_ARRAY_UINT64:
		rc = print_telemetry_rec_array(buf, rec);
		break;
	case MIO_TM_TYPE_NONE:
		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc == 0)
		printf("* %s\n", buf);
	return rc;
}

int parse(FILE *fp)
{
	int rc = 0;
	struct mio_telemetry_store store;
	struct mio_telemetry_rec rec;

	store.mts_parse_stream = (void *)fp;
	while (1) {
		rc = mio_telemetry_parse(&store, &rec);
		if (rc == EOF) {
			rc = 0;
			break;
		} else if (rc < 0)
			continue;

		print_telemetry_rec(&rec);
	}

	return rc;
}

int main(int argc, char **argv)
{
	char *fname;
	FILE *fp;
	struct mio_telemetry_conf conf;
	enum mio_telemetry_store_type type;

	if (argc < 3) {
		usage();
		exit(EXIT_FAILURE);
	}
	fname = argv[1];
	fp = fopen(fname, "r");
	if (fp == NULL) {
		fprintf(stderr, "Can't open file %s.\n", fname);
		exit(EXIT_FAILURE);
	}

#ifdef MIO_MOTR_ADDB
	if (!strcmp(argv[2], "addb"))
		type = MIO_TM_ST_ADDB;
	else
#endif
	if (!strcmp(argv[2], "log"))
		type = MIO_TM_ST_LOG;
	else {
		fprintf(stderr,
			"Unsupported telemetry store type: %s.",
			argv[2]);
		usage();
		exit(EXIT_FAILURE);
	}
	conf.mtc_type = type;
	conf.mtc_is_parser = true;

	mio_telemetry_init(&conf);
	parse(fp);
	mio_telemetry_fini();

	return 0;
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
