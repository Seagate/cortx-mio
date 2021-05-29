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
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "logger.h"
#include "utils.h"
#include "mio_internal.h"
#include "mio_telemetry.h"

/**
 * Notes for storing and parsing telemetry records as MIO log entry.
 *
 * (1) MIO configuration yaml file has an option to select different
 *     telemetry backend type. Set to LOG if text-based logging is
 *     preferred.
 *
 * (2) To make the log entry readable, the following record format is used:
 *
 *     [TELEM] TIMESTAMP MIO_TELEMETRY_TOPIC DATA_TYPE VALUES
 *
 *     in which DATA_TYPE is the text string representing data type. 
 */

char *telem_data_type_names[] = {
	[MIO_TM_TYPE_UINT16] = "UINT16",
	[MIO_TM_TYPE_UINT32] = "UINT32",
	[MIO_TM_TYPE_UINT64] = "UINT64",
	[MIO_TM_TYPE_TIMESPAN] = "TIMESPAN",
	[MIO_TM_TYPE_TIMEPOINT] = "TIMEPOINT",
	[MIO_TM_TYPE_STRING] = "STRING",
	[MIO_TM_TYPE_ARRAY_UINT16] = "ARRAY_UINT16",
	[MIO_TM_TYPE_ARRAY_UINT32] = "ARRAY_UINT32",
	[MIO_TM_TYPE_ARRAY_UINT64] = "ARRAY_UINT64",
};

static void telem_log_skip_delimiter(char **buf)
{
	int len;
	char *cursor;

	len = strlen(*buf);
	cursor = *buf;
	while (isspace(*cursor)) {
		cursor++;
		if (cursor - (*buf) >= len)
			break;
	}
	*buf = cursor;
}

static void telem_log_jump_to_delimiter(char **buf)
{
	int len;
	char *cursor;

	len = strlen(*buf);
	cursor = *buf;
	while (!isspace(*cursor) && *cursor != '\n') {
		cursor++;
		if (cursor - (*buf) >= len)
			break;
	}
	*buf = cursor;
}

static void telem_log_rec_get_string(char **rec, char **string)
{
	uint8_t len;
	char *end_of_str;

	*string = NULL;

	telem_log_skip_delimiter(rec);
	end_of_str = *rec;
	telem_log_jump_to_delimiter(&end_of_str);
	len = end_of_str - *rec;

	/* The string stored doesn't include '\0', add it back. */
	*string = mio_mem_alloc(len + 1);
	if (*string == NULL)
		return;
	mio_mem_copy(*string, *rec, len);
	(*string)[len] = '\0';
	*rec += len;
}

static void
telem_log_rec_get_scalar(char **rec, enum mio_telemetry_type type, void *val)
{
	int rc;
	char val_str[32];
	char *end_of_val;
	int val_len;

	end_of_val = *rec;
	telem_log_skip_delimiter(&end_of_val);
	telem_log_jump_to_delimiter(&end_of_val);
	val_len = end_of_val - *rec;
	if (val_len <= 0 || val_len > 32) {
		fprintf(stderr, "Value is too big!\n");
		goto exit;		
	}
	mio_mem_copy(val_str, *rec, end_of_val - (*rec));

	switch (type) {
	case MIO_TM_TYPE_UINT16:
		*(uint16_t *)val = (uint16_t)atoi(val_str);
		break;
	case MIO_TM_TYPE_UINT32:
		*(uint32_t *)val = (uint32_t)atoi(val_str);
		break;
	case MIO_TM_TYPE_UINT64:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_TIMEPOINT:
		rc = sscanf(val_str, "%"SCNu64"", (uint64_t *)val);
		if (rc <= 0)
			fprintf(stderr, "Can't parse uint64 value!\n");
		break;
	default:
		fprintf(stderr, "Wrong data type for scalar value!\n");
		break;
	}

exit:
	*rec = end_of_val;
}

static void telem_log_rec_get_u16(char **rec, uint16_t *val)
{
	telem_log_rec_get_scalar(rec, MIO_TM_TYPE_UINT16, val);
}

static void telem_log_rec_get_u32(char **rec, uint32_t *val)
{
	telem_log_rec_get_scalar(rec, MIO_TM_TYPE_UINT32, val);
}

static void telem_log_rec_get_u64(char **rec, uint64_t *val)
{
	telem_log_rec_get_scalar(rec, MIO_TM_TYPE_UINT64, val);
}

static int
telem_log_rec_get_array(char **rec, enum mio_telemetry_type type,
			struct mio_telemetry_array *array)
{
	int i;
	uint16_t nr_elms;
	uint16_t nr_bytes;
	void *elms;

	telem_log_rec_get_u16(rec, &nr_elms);
	if (type == MIO_TM_TYPE_ARRAY_UINT16)
		nr_bytes = nr_elms * 2; 
	else if (type == MIO_TM_TYPE_ARRAY_UINT32)
		nr_bytes = nr_elms * 4; 
	else if (type == MIO_TM_TYPE_ARRAY_UINT64)
		nr_bytes = nr_elms * 8; 
	else
		return -EINVAL;

	elms = mio_mem_alloc(nr_bytes);
	if (elms == NULL)
		return -ENOMEM;

	for (i = 0; i < nr_elms; i++) {
		if (type == MIO_TM_TYPE_ARRAY_UINT16)
			telem_log_rec_get_u16(rec, &((uint16_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT32)
			telem_log_rec_get_u32(rec, &((uint32_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT64)
			telem_log_rec_get_u64(rec, &((uint64_t *)elms)[i]);
	}
	array->mta_nr_elms = nr_elms;
	array->mta_elms = elms;
	return 0;
}

static int
telem_log_rec_get_value(char **rec, enum mio_telemetry_type type, void **value)
{
	int rc = 0;

	*value = NULL;
	if (type == MIO_TM_TYPE_NONE)
		return 0;

	rc = mio_telemetry_alloc_value(type, value);
	if (rc < 0)
		return rc;

	switch (type) {
	case MIO_TM_TYPE_UINT16:
		telem_log_rec_get_u16(rec, (uint16_t *)(*value));
		break;
	case MIO_TM_TYPE_UINT32:
		telem_log_rec_get_u32(rec, (uint32_t *)(*value));
		break;
	case MIO_TM_TYPE_UINT64:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_TIMEPOINT:
		telem_log_rec_get_u64(rec, (uint64_t *)(*value));
		break;
	case MIO_TM_TYPE_ARRAY_UINT16:
	case MIO_TM_TYPE_ARRAY_UINT32:
	case MIO_TM_TYPE_ARRAY_UINT64:
		telem_log_rec_get_array(
			rec, type, (struct mio_telemetry_array *)(*value));
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int
telem_log_rec_get_value_type(char **rec, enum mio_telemetry_type *type)
{
	int i;
	char *type_str;

	telem_log_rec_get_string(rec, &type_str);
	if (type_str == NULL)
		return -ENOMEM;
	for (i = MIO_TM_TYPE_UINT16; i < MIO_TM_TYPE_NR; i++)
		if (!strcmp(type_str, telem_data_type_names[i]))
			break;
	if (i == MIO_TM_TYPE_NR)
		return -EINVAL;

	mio_mem_free(type_str);
	*type = i;
	return 0;
}

static void telem_log_rec_add_string(char **rec, const char *string)
{
	uint8_t str_len;

	assert(string != NULL);
	str_len = strlen(string);
	mio_mem_copy(*rec, (char *)string, str_len);
	*rec += str_len;
	**rec = ' ';
	*rec += 1;
}

static void telem_log_rec_add_u16(char **rec, uint16_t val)
{
	char val_str[16];

	sprintf(val_str, "%u", val);
	telem_log_rec_add_string(rec, val_str);
}

static void telem_log_rec_add_u32(char **rec, uint32_t val)
{
	char val_str[32];

	sprintf(val_str, "%u", val);
	telem_log_rec_add_string(rec, val_str);
}

static void telem_log_rec_add_u64(char **rec, uint64_t val)
{
	char val_str[64];

	sprintf(val_str, "%"PRIu64"", val);
	telem_log_rec_add_string(rec, val_str);
}

static int
telem_log_rec_add_array(char **rec, enum mio_telemetry_type type, void *value)
{
	int i;
	int nr_elms = 0;
	void *elms;
	struct mio_telemetry_array *arr;

	assert(value != NULL && *rec != NULL);
	arr = (struct mio_telemetry_array *)value;
	nr_elms = arr->mta_nr_elms;

	/* Number of elements. */
	telem_log_rec_add_u16(rec, (uint16_t)nr_elms);

	/* Elements */
	elms = arr->mta_elms;
	for (i = 0; i < nr_elms; i++) {
		if (type == MIO_TM_TYPE_ARRAY_UINT16)
			telem_log_rec_add_u16(rec, ((uint16_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT32)
			telem_log_rec_add_u32(rec, ((uint32_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT64)
			telem_log_rec_add_u64(rec, ((uint64_t *)elms)[i]);
	}

	return 0;
}

static int
telem_log_rec_add_value(char **rec, enum mio_telemetry_type type, void *value)
{
	int rc = 0;

	assert(type > MIO_TM_TYPE_INVALID && type < MIO_TM_TYPE_NR);

	switch (type) {
	case MIO_TM_TYPE_UINT16:
		telem_log_rec_add_u16(rec, *((uint16_t *)value));
		break;
	case MIO_TM_TYPE_UINT32:
		telem_log_rec_add_u32(rec, *((uint32_t *)value));
		break;
	case MIO_TM_TYPE_TIMEPOINT:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_UINT64:
		telem_log_rec_add_u64(rec, *((uint64_t *)value));
		break;
	case MIO_TM_TYPE_ARRAY_UINT16:
	case MIO_TM_TYPE_ARRAY_UINT32:
	case MIO_TM_TYPE_ARRAY_UINT64:
		rc = telem_log_rec_add_array(rec, type, value);
		break;
	case MIO_TM_TYPE_NONE:
		/* Do nothing. */
		break;
	default:
		telem_log_rec_add_u64(rec, *((uint64_t *)value));
		break;
	}

	return rc;
}

static int
mio_telem_log_encode(const struct mio_telemetry_rec *rec, char **buf, int *len)
{
	int rc = 0;
	int rec_len;
	char *cursor;	
	const char *topic;
	enum mio_telemetry_type type;
	void *value;
	char *rec_buf;

	topic = rec->mtr_topic;
	type = rec->mtr_type;
	value = rec->mtr_value;
	if (type <= MIO_TM_TYPE_INVALID || type >= MIO_TM_TYPE_NR)
		return -EINVAL;

	rec_buf = mio_mem_alloc(MIO_LOG_MAX_REC_LEN);
	if (rec_buf == NULL)
		return -ENOMEM;
	cursor = rec_buf;

	/* Topic. */
	telem_log_rec_add_string(&cursor, topic);

	/* Type. */
	telem_log_rec_add_string(&cursor, telem_data_type_names[type]);

	/* Value. */
	rc = telem_log_rec_add_value(&cursor, type, value);
	if (rc < 0) {
		mio_mem_free(rec_buf);
		rec_buf = NULL;
		goto exit;
	}

	rec_len = strlen(rec_buf) + 1;
	rec_buf = realloc(rec_buf, rec_len);
	*len = rec_len;
exit:
	*buf = rec_buf;
	return rc;

}

static int mio_telem_log_decode(const char *buf, const char *head,
				const char *tail, struct mio_telemetry_rec *rec)
{
	int rc = 0;
	char *cursor;	
	char *topic = NULL;
	enum mio_telemetry_type type = MIO_TM_TYPE_INVALID;
	void *value = NULL;

	if (buf == NULL || head == NULL || rec == NULL)
		return -EINVAL;

	/* Time string. */
	rec->mtr_time_str = mio_mem_alloc(strlen(head) + 1);
	if (rec->mtr_time_str == NULL)
		return -ENOMEM;
	mio_mem_copy(rec->mtr_time_str, (char *)head, strlen(head) + 1);

	/* Topic. */
	cursor = (char *)buf;
	telem_log_rec_get_string(&cursor, &topic);

	/* Type. */
	telem_log_rec_get_value_type(&cursor, &type);

	/* Value. */
	rc = telem_log_rec_get_value(&cursor, type, &value);
	if (rc < 0) {
		mio_mem_free(rec->mtr_time_str);
		mio_mem_free(topic);
		return rc;
	}

	rec->mtr_topic = topic;
	rec->mtr_type = type;
	rec->mtr_value = value;

	return rc;
}

static int mio_telem_log_store(void *dump_stream, const char *buf, int len)
{
	mio_log(MIO_TELEMETRY, "%s\n", buf);
	return 0;
}

static int mio_telem_log_load(void *parse_stream, char **rec_buf,
			      char **head, char **tail)
{
	int rc = 0;
	int time_str_len = 0;
	size_t line_len = 0;
	size_t line_cap = 0;
	char *time_str = NULL;
	char *line = NULL;
	char *clock;
	char *rec = NULL;
	FILE *fp = (FILE *)parse_stream;

	assert(fp != NULL);

	while ((line_len = getline(&line, &line_cap, fp)) != EOF) {
		/* Skip those lines not starting with `[telem]`. */
		if (strstr(line, mio_log_levels[MIO_TELEMETRY].name) == NULL)
			continue;

		clock = line + strlen(mio_log_levels[MIO_TELEMETRY].name) + 2;
		telem_log_skip_delimiter(&clock);
		rec = clock;
		telem_log_jump_to_delimiter(&rec);
		time_str_len = rec - clock;

		/* Copy time string and add '\0' at the end. */
		time_str = mio_mem_alloc(time_str_len + 1);
		if (time_str == NULL)
			break;
		mio_mem_copy(time_str, clock, time_str_len);
		time_str[time_str_len] = '\0';
		break;
	}
	if (line_len == EOF)
		return EOF;
	if (time_str == NULL)
		return -ENOMEM;

	*rec_buf = mio_mem_alloc(line_len - time_str_len);
	if (*rec_buf == NULL)
		goto exit;
	mio_mem_copy(*rec_buf, rec, line_len - time_str_len);
	*head = time_str;

exit:
	mio_mem_free(line);
	return rc;

}

struct mio_telemetry_rec_ops mio_telem_log_rec_ops = {
        .mtro_encode         = mio_telem_log_encode,
        .mtro_decode         = mio_telem_log_decode,
        .mtro_store          = mio_telem_log_store,
        .mtro_load           = mio_telem_log_load
};

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
 *
 */
