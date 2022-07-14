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
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <addb2/addb2_internal.h>

#include "utils.h"
#include "mio_internal.h"
#include "mio_telemetry.h"

/**
 * Notes for telemetry ADDB implementation:
 *
 * (1) Packing telemetry records into ADDB.
 *
 * Cited from motr:
 *
 * All addb2 records have the same structure: a record (struct m0_addb2_record)
 * is a "measurement", tagged with a set of "labels". Measurements and labels
 * have the same structure, described by struct m0_addb2_value: a 56-bit
 * identifier, plus a time-stamp, plus a (variable up to 15, possibly 0) number
 * of 64-bit data items, called "payload".
 * 
 * MIO ADDB telemetry backend packs a record {topic, type, value} into the
 * payload of 15*64bits (120bytes):
 * (1) 2 bytes of Maestro magic number (0x202E).
 * (2) 1 byte of topic length followed by the `topic` string.
 * (3) 1 byte of record `type` followed by `value` in byte's order.
 * (4) If the `value` if of type array, MIO uses the first byte to store
 *     the number of elements in the array.
 *
 * -E2BIG error will be returned if the length of a packed record is greater
 * than the payload limit. 
 * 
 * (2) Parsing ADDB records.
 *
 * Instead of implementing a ADDB plugin to parse MIO specific ADDB records,
 * MIO filters MIO generated records out from ADDB dump by selecting only
 * those records with MIO magic number and ID.
 *
 * This is because ADDB plugin only supports parsing fields of uint64_t each
 * time, but MIO supports many other types, such as string and array.
 * Downside of this method is that MIO telemetry implemenation is bound with
 * ADDB output format, any change in ADDB dump output format, this part of
 * code in MIO has to be changed accordingly.
 *
 * Steps to parse ADDB records.
 * (1) Use ADDB dump utility to parse ADDB records and outputs as lines of text.
 * (2) MIO telemetry parser picks MIO specific records only and removes added
 *     items (such as "|" which is used to separate payload.)
 * (3) Decode records.
 */

uint16_t motr_addb_magic = 0x202e;

enum {
	MOTR_ADDB_NO_PREFIX_SIGN = 0,
	MOTR_ADDB_PREFIX_SIGN = 1,
};

enum {
	MIO_MOTR_ADDB_ID = M0_ADDB2__EXT_RANGE_3,
	MIO_MOTR_ADDB_MAX_PAYLOAD = 120
};

static int motr_addb_rec_value_len(enum mio_telemetry_type type, void *value)
{
	int len = -EINVAL;
	struct mio_telemetry_array *arr;

	switch (type) {
	case MIO_TM_TYPE_UINT16:
		len = 2;
		break;
	case MIO_TM_TYPE_UINT32:
		len = 4;
		break;
	case MIO_TM_TYPE_UINT64:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_TIMEPOINT:
		len = 8;
		break;
	case MIO_TM_TYPE_ARRAY_UINT16:
		arr = (struct mio_telemetry_array *)value;
		len = arr->mta_nr_elms * sizeof(uint16_t);
		break;
	case MIO_TM_TYPE_ARRAY_UINT32:
		arr = (struct mio_telemetry_array *)value;
		len = arr->mta_nr_elms * sizeof(uint32_t);
		break;
	case MIO_TM_TYPE_ARRAY_UINT64:
		arr = (struct mio_telemetry_array *)value;
		len = arr->mta_nr_elms * sizeof(uint64_t);
		break;
	default:
		len = -EINVAL;
		break;
	}

	return len;
}

static int motr_addb_rec_len(const char *prefix, const char *topic,
			     enum mio_telemetry_type type,
			     void *value)
{
	int prefix_len = 0;
	int topic_len;
	int val_len;
	int buf_len;

	if (prefix != NULL)
		prefix_len = strnlen(prefix, MIO_MOTR_ADDB_MAX_PAYLOAD); 
	assert(topic != NULL);
	topic_len = strnlen(topic, MIO_MOTR_ADDB_MAX_PAYLOAD);
	val_len = motr_addb_rec_value_len(type, value);
	if (val_len < 0)
		return val_len;
	/*
	 * Magic: 2 bytes
	 * Prefix: 1 byte for `sign`, 1 byte for length + prefix length
	 * topic: 1 byte(lenght) + string length,
	 * value: 1 byte(length) + value size
	 */
	buf_len = 2 + prefix_len + 2 + topic_len + 1 + val_len + 1;
	return buf_len;
}


static void motr_addb_rec_get_u8(char **rec, uint8_t *val)
{
	mio_mem_copy(val, *rec, sizeof(uint8_t));
	*rec += sizeof(uint8_t);
}

static void motr_addb_rec_get_u16(char **rec, uint16_t *val)
{
	uint16_t le16_val;

	mio_mem_copy(&le16_val, *rec, sizeof(uint16_t));
	*val = mio_byteorder_le16_to_cpu(le16_val);
	*rec += sizeof(uint16_t);
}

static void motr_addb_rec_get_u32(char **rec, uint32_t *val)
{
	uint32_t le32_val;

	mio_mem_copy(&le32_val, *rec, sizeof(uint32_t));
	*val = mio_byteorder_le32_to_cpu(le32_val);
	*rec += sizeof(uint32_t);
}

static void motr_addb_rec_get_u64(char **rec, uint64_t *val)
{
	uint64_t le64_val;

	mio_mem_copy(&le64_val, *rec, sizeof(uint64_t));
	*val = mio_byteorder_le64_to_cpu(le64_val);
	*rec += sizeof(uint64_t);
}

static int
motr_addb_rec_get_array(char **rec, enum mio_telemetry_type type,
			struct mio_telemetry_array *array)
{
	int i;
	uint8_t nr_elms;
	int max_nr_elms;
	uint8_t nr_bytes;
	void *elms;

	motr_addb_rec_get_u8(rec, &nr_elms);
	if (type == MIO_TM_TYPE_ARRAY_UINT16) {
		nr_bytes = nr_elms * 2; 
		max_nr_elms = MIO_MOTR_ADDB_MAX_PAYLOAD / 2;
	} else if (type == MIO_TM_TYPE_ARRAY_UINT32) {
		nr_bytes = nr_elms * 4; 
		max_nr_elms = MIO_MOTR_ADDB_MAX_PAYLOAD / 4;
	} else if (type == MIO_TM_TYPE_ARRAY_UINT64) {
		nr_bytes = nr_elms * 8; 
		max_nr_elms = MIO_MOTR_ADDB_MAX_PAYLOAD / 8;
	} else
		return -EINVAL;

	if (nr_elms >= max_nr_elms)
		return -EINVAL;

	elms = mio_mem_alloc(nr_bytes);
	if (elms == NULL)
		return -ENOMEM;

	for (i = 0; i < nr_elms; i++) {
		if (type == MIO_TM_TYPE_ARRAY_UINT16)
			motr_addb_rec_get_u16(rec, &((uint16_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT32)
			motr_addb_rec_get_u32(rec, &((uint32_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT64)
			motr_addb_rec_get_u64(rec, &((uint64_t *)elms)[i]);
	}
	array->mta_nr_elms = nr_elms;
	array->mta_elms = elms;
	return 0;
}

static void motr_addb_rec_get_string(char **rec, char **string)
{
	uint8_t len;

	*string = NULL;

	motr_addb_rec_get_u8(rec, &len);
	if (len == 0 || len >= MIO_MOTR_ADDB_MAX_PAYLOAD)
		return;

	/* The string stored doesn't include '\0', add it back. */
	*string = mio_mem_alloc(len + 1);
	if (*string == NULL)
		return;
	mio_mem_copy(*string, *rec, len);
	(*string)[len] = '\0';
	*rec += len;
}

static int
motr_addb_rec_get_value(char **rec, enum mio_telemetry_type type, void **value)
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
		motr_addb_rec_get_u16(rec, (uint16_t *)(*value));
		break;
	case MIO_TM_TYPE_UINT32:
		motr_addb_rec_get_u32(rec, (uint32_t *)(*value));
		break;
	case MIO_TM_TYPE_UINT64:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_TIMEPOINT:
		motr_addb_rec_get_u64(rec, (uint64_t *)(*value));
		break;
	case MIO_TM_TYPE_ARRAY_UINT16:
	case MIO_TM_TYPE_ARRAY_UINT32:
	case MIO_TM_TYPE_ARRAY_UINT64:
		motr_addb_rec_get_array(
			rec, type, (struct mio_telemetry_array *)(*value));
		break;
	default:
		rc = -EINVAL;
		break;
	}

	return rc;
}

static void motr_addb_rec_add_u8(char **rec, uint8_t val)
{
	mio_mem_copy(*rec, &val, sizeof(uint8_t));
	*rec += sizeof(uint8_t);
}

static void motr_addb_rec_add_u16(char **rec, uint16_t val)
{
	uint16_t le16_val;

	le16_val = mio_byteorder_cpu_to_le16(val);
	mio_mem_copy(*rec, &le16_val, sizeof(uint16_t));
	*rec += sizeof(uint16_t);
}

static void motr_addb_rec_add_u32(char **rec, uint32_t val)
{
	uint32_t le32_val;

	le32_val = mio_byteorder_cpu_to_le32(val);
	mio_mem_copy(*rec, &le32_val, sizeof(uint32_t));
	*rec += sizeof(uint32_t);
}

static void motr_addb_rec_add_u64(char **rec, uint64_t val)
{
	uint64_t le64_val;

	le64_val = mio_byteorder_cpu_to_le64(val);
	mio_mem_copy(*rec, &le64_val, sizeof(uint64_t));
	*rec += sizeof(uint64_t);
}

static int
motr_addb_rec_add_array(char **rec, enum mio_telemetry_type type, void *value)
{
	int i;
	int nr_elms = 0;
	int max_nr_elms = 0;
	void *elms;
	struct mio_telemetry_array *arr;

	assert(value != NULL && *rec != NULL);
	arr = (struct mio_telemetry_array *)value;

	if (type == MIO_TM_TYPE_ARRAY_UINT16)
		max_nr_elms = MIO_MOTR_ADDB_MAX_PAYLOAD / 2;
	else if (type == MIO_TM_TYPE_ARRAY_UINT32)
		max_nr_elms = MIO_MOTR_ADDB_MAX_PAYLOAD / 4;
	else if (type == MIO_TM_TYPE_ARRAY_UINT64)
		max_nr_elms = MIO_MOTR_ADDB_MAX_PAYLOAD / 8;
	else
		return -EINVAL;
	nr_elms = arr->mta_nr_elms;
	if (nr_elms <= 0 || nr_elms >= max_nr_elms)
		return -EINVAL;

	/* Length */
	motr_addb_rec_add_u8(rec, (uint8_t)nr_elms);

	/* Elements */
	elms = arr->mta_elms;
	for (i = 0; i < nr_elms; i++) {
		if (type == MIO_TM_TYPE_ARRAY_UINT16)
			motr_addb_rec_add_u16(rec, ((uint16_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT32)
			motr_addb_rec_add_u32(rec, ((uint32_t *)elms)[i]);
		else if (type == MIO_TM_TYPE_ARRAY_UINT64)
			motr_addb_rec_add_u64(rec, ((uint64_t *)elms)[i]);
	}

	return 0;
}

static void motr_addb_rec_add_string(char **rec, const char *string)
{
	uint8_t str_len;

	str_len = strnlen(string, MIO_MOTR_ADDB_MAX_PAYLOAD);
	motr_addb_rec_add_u8(rec, str_len);

	mio_mem_copy(*rec, (char *)string, str_len);
	*rec += str_len;
}

static int
motr_addb_rec_add_value(char **rec, enum mio_telemetry_type type, void *value)
{
	int rc = 0;

	assert(type > MIO_TM_TYPE_INVALID && type < MIO_TM_TYPE_NR);

	switch (type) {
	case MIO_TM_TYPE_UINT16:
		motr_addb_rec_add_u16(rec, *((uint16_t *)value));
		break;
	case MIO_TM_TYPE_UINT32:
		motr_addb_rec_add_u32(rec, *((uint32_t *)value));
		break;
	case MIO_TM_TYPE_TIMEPOINT:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_UINT64:
		motr_addb_rec_add_u64(rec, *((uint64_t *)value));
		break;
	case MIO_TM_TYPE_ARRAY_UINT16:
	case MIO_TM_TYPE_ARRAY_UINT32:
	case MIO_TM_TYPE_ARRAY_UINT64:
		rc = motr_addb_rec_add_array(rec, type, value);
		break;
	case MIO_TM_TYPE_NONE:
		/* Do nothing. */
		break;
	default:
		motr_addb_rec_add_u64(rec, *((uint64_t *)value));
		break;
	}

	return rc;
}

static int
mio_motr_addb_encode(const struct mio_telemetry_rec *rec, char **buf, int *len)
{
	int i;
	int rc = 0;
	int rec_len;
	char *cursor;	
	char *prefix;
	const char *topic;
	enum mio_telemetry_type type;
	void *value;
	char *encoded_buf;

	prefix = rec->mtr_prefix;
	topic = rec->mtr_topic;
	type = rec->mtr_type;
	value = rec->mtr_value;
	if (type <= MIO_TM_TYPE_INVALID || type >= MIO_TM_TYPE_NR)
		return -EINVAL;

	rec_len = motr_addb_rec_len(prefix, topic, type, value);
	if (rec_len < 0)
		return rec_len;
	rec_len = ((rec_len + 7) / 8) * 8; /* Rounded to multiples of 8*/
	encoded_buf = mio_mem_alloc(rec_len);
	if (encoded_buf == NULL)
		return -ENOMEM;
	cursor = encoded_buf;

	/* Magic number. */
	motr_addb_rec_add_u16(&cursor, motr_addb_magic);

	/* Prefix. */
        if (prefix != NULL) {
		motr_addb_rec_add_u8(&cursor, MOTR_ADDB_PREFIX_SIGN);
                motr_addb_rec_add_string(&cursor, prefix);
        } else
                motr_addb_rec_add_u8(&cursor, MOTR_ADDB_NO_PREFIX_SIGN);

	/* Topic. */
	motr_addb_rec_add_string(&cursor, topic);

	/* Type. */
	motr_addb_rec_add_u8(&cursor, type);

	/* Value. */
	rc = motr_addb_rec_add_value(&cursor, type, value);
	if (rc < 0) {
		mio_mem_free(encoded_buf);
		encoded_buf = NULL;
		goto exit;
	}

	/* Pad the record to be multiple of 8 bytes long. */
	for (i = 0; i < rec_len - (cursor - encoded_buf); i++)
		cursor[i] = 0x00;

	*len = rec_len;
exit:
	*buf = encoded_buf;
	return rc;
}

static int mio_motr_addb_store(void *dump_stream, const char *buf, int len)
{
	int nr_u64;

	nr_u64 = len / sizeof(uint64_t);
	m0_addb2_add(MIO_MOTR_ADDB_ID, nr_u64, (uint64_t *)buf);

	return 0;
}

static int mio_motr_addb_decode(const char *buf, const char *head,
				const char *tail, struct mio_telemetry_rec *rec)
{
	int rc = 0;
	uint16_t magic;
	uint8_t type_u8;
	char *cursor;
	char *prefix = NULL;
	char *topic = NULL;
	uint8_t has_prefix;
	enum mio_telemetry_type type;
	void *value = NULL;

	if (buf == NULL || head == NULL || rec == NULL)
		return -EINVAL;

	/* Time string. */
	rec->mtr_time_str =
	    mio_mem_alloc(strnlen(head, MIO_MOTR_ADDB_MAX_PAYLOAD) + 1);
	if (rec->mtr_time_str == NULL)
		return -ENOMEM;
	mio_mem_copy(rec->mtr_time_str, (char *)head,
		     strnlen(head, MIO_MOTR_ADDB_MAX_PAYLOAD) + 1);

	/* Magic number. */
	cursor = (char *)buf;
	motr_addb_rec_get_u16(&cursor, &magic);
	if (magic != motr_addb_magic)
		return -EINVAL;

        /* Prefix. */
        motr_addb_rec_get_u8(&cursor, &has_prefix);
        if (has_prefix == MOTR_ADDB_PREFIX_SIGN)
                motr_addb_rec_get_string(&cursor, &prefix);

	/* Topic. */
	motr_addb_rec_get_string(&cursor, &topic);

	/* Type. */
	motr_addb_rec_get_u8(&cursor, &type_u8);
	type = type_u8;

	/* Value. */
	rc = motr_addb_rec_get_value(&cursor, type, &value);
	if (rc < 0) {
		mio_mem_free(rec->mtr_time_str);
		mio_mem_free(topic);
		return rc;
	}

	rec->mtr_prefix = prefix;
	rec->mtr_topic = topic;
	rec->mtr_type = type;
	rec->mtr_value = value;
	return rc;
}

static void motr_addb_skip_delimiter(char **buf)
{
	int len;
	char *cursor;

	len = strnlen(*buf, MIO_MOTR_ADDB_MAX_PAYLOAD);
	cursor = *buf;
	while (isspace(*cursor) || *cursor == '?' || *cursor == ',') {
		cursor++;
		if (cursor - (*buf) >= len)
			break;
	}
	*buf = cursor;
}

static void motr_addb_jump_to_delimiter(char **buf)
{
	int len;
	char *cursor;

	len = strnlen(*buf, MIO_MOTR_ADDB_MAX_PAYLOAD);
	cursor = *buf;
	while (!isspace(*cursor) && *cursor != '?' && *cursor != ',') {
		cursor++;
		if (cursor - (*buf) >= len)
			break;
	}
	*buf = cursor;
}

static int motr_addb_clean_rec(char *rec_buf, int rec_len, char **cleaned_rec)
{
	int rc = 0;
	int cleaned_rec_len = 0;
	uint64_t value;
	char *end_of_value;
	char *end_of_rec;
	char *cursor;
	char *tmp_rec;

	tmp_rec = mio_mem_alloc(rec_len);
	if (tmp_rec == NULL)
		return -ENOMEM;
	/*
	 * As MIO doesn't provide a plugin for ADDB dump utility to parse
	 * records, MIO telemetry record is output as a set of fields in
	 * the format of "?"%16"PRIx64"?", separating by ','.
	 *
	 * So, we need to remove all '?' and ',' and convert string to uint64_t.
 	 */
	cursor = rec_buf;
	end_of_rec = rec_buf + rec_len;
	while(cursor != end_of_rec) {
		motr_addb_skip_delimiter(&cursor);
		rc = sscanf(cursor, "%"SCNx64"", &value);
		if (rc <= 0)
			break;

		end_of_value = cursor;
		motr_addb_jump_to_delimiter(&end_of_value);

		mio_mem_copy(tmp_rec + cleaned_rec_len, &value, sizeof(value));
		cursor = end_of_value;
		cleaned_rec_len += sizeof(value);
	}

	*cleaned_rec = realloc(tmp_rec, cleaned_rec_len);
	return rc;
}

/*
 * Notes for ADDB dump output format:
 *
 * (1) Start with '*' followed by ADDB measurement, the measurement is
 *     generated from the type `m0_addb2_value`.
 *
 *     struct m0_addb2_value {
 *             uint64_t        va_id;
 *             uint64_t        va_time;
 *             unsigned        va_nr;
 *             const uint64_t *va_data;
 *    };
 *
 *    time is the first field and printed as a time string.
 *    ID is the 2nd field and output as corresponding name.
 *
 * (2) A number of labels starting with '|'.
 *
 * MIO will decode the measurement part and leave the label part
 * unchanged and copy to the `rest` pointer. MIO is only interested in
 * the time and data these 2 fields.
 *
 */
static int mio_motr_addb_load(void *parse_stream, char **rec_buf,
			      char **head, char **tail)
{
	int rc = 0;
	int time_str_len = 0;
	size_t line_len = 0;
	size_t line_cap = 0;
	size_t rec_len = 0;
	char *time_str = NULL;
	char *dirty_rec;
	char *cleaned_rec = NULL;
	char *line = NULL;
	char *end_of_rec = NULL;
	char *clock;
	char *addb_id;
	char *st_of_rec = NULL;
	uint64_t id = 0;
	FILE *fp = (FILE *)parse_stream;

	assert(fp != NULL);

	while ((line_len = getline(&line, &line_cap, fp)) != EOF) {
		/* Skip those lines not starting with `*`. */
		if (line[0] != '*')
			continue;
		
		clock = line + 1;
		motr_addb_skip_delimiter(&clock);
		addb_id = clock;
		motr_addb_jump_to_delimiter(&addb_id);
		time_str_len = addb_id - clock;

		/* Check ADDB ID: ID must be MIO_MOTR_ADDB_ID. */
		motr_addb_skip_delimiter(&addb_id);
		rc = sscanf(addb_id, "%"SCNx64"", &id);
		if (rc > 0 && id == MIO_MOTR_ADDB_ID) {
			/* Copy time string and add '\0' at the end. */
			time_str = mio_mem_alloc(time_str_len + 1);
			if (time_str == NULL)
				break;
			mio_mem_copy(time_str, clock, time_str_len);
			time_str[time_str_len] = '\0';

			st_of_rec = addb_id;
			motr_addb_jump_to_delimiter(&st_of_rec);
			break;
		}
	}
	if (line_len == EOF)
		return EOF;
	if (id != MIO_MOTR_ADDB_ID)
		return -EINVAL;
	if (time_str == NULL)
		return -ENOMEM;

	/* Search for the start (`|`) of ADDB record labels. */
	end_of_rec = strchr(st_of_rec, '|');
	if (end_of_rec == NULL)
		end_of_rec = line + strnlen(line, MIO_MOTR_ADDB_MAX_PAYLOAD);

	/* Now for the real telemetry data. */
	dirty_rec = st_of_rec;
	rec_len = end_of_rec - st_of_rec;
	motr_addb_clean_rec(dirty_rec, rec_len, &cleaned_rec);
	if (cleaned_rec == NULL) {
		rc = -EIO;
		mio_mem_free(time_str);
		goto exit;	
	}
	*head = time_str;
	*rec_buf = cleaned_rec;

exit:
	mio_mem_free(line);
	return rc;
}

struct mio_telemetry_rec_ops mio_motr_addb_rec_ops = {
        .mtro_encode         = mio_motr_addb_encode,
        .mtro_decode         = mio_motr_addb_decode,
        .mtro_store          = mio_motr_addb_store,
        .mtro_load           = mio_motr_addb_load
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
