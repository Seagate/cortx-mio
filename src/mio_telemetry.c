/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

/**
 * Telemetry interface provides MIO and its upper applications (for example,
 * Maestro core and Maestro workflow manager etc.) with APIs to encode/decode
 * telmetry records and to store/load/parse records to/from the same storage
 * backend.
 *
 * (1) Instead of allowing applications to generate any self-defined record
 *     structure, telemetry interface defines a set of simple and common data
 *     types, such as UINT64, STRING and UINT16_LIST etc.. For a self-defined
 *     record structure, applications will have to also generate a description
 *     of this data structure (such as those defined by protobuf or json) and
 *     convey to MIO telemetry in order to encode and decode the structure.
 *     This adds the burden for applications.
 *
 *     Using Common data types removes the need to generate record structure
 *     description although it may be more difficult to output a complex
 *     data structure. But common data types defined in this interface should
 *     be suffice for most of cases.
 *
 * (2) Based on the common record types defined above, a telemetry record is
 *     defined as a tuple of form {topic, record type, value}
 *
 *     `topic` is defined as a string.
 *
 * (3) Telemetry API is defined as below:
 *
 *     mio_telemetry_advertise(const char *topic,
 *     			       enum mio_telemetry_data_type, void *data);
 *
 * (4) A generic backend interface to store/load/parse records. 
 *     As Motr offers its own data instrumentation mechanism ADDB, when Motr
 *     is used as MIO backend storage, ADDB is used to generate/collect/parse
 *     telemetry data. It essentially gathers the whole stack's (from Maestro
 *     application, workflow manager, Maestro core to Motr) telemetry data
 *     into a common place, making the analysis more convinient and
 *     also offering more analysis possibilities.
 *
 *     If Motr is not used, it stores records using MIO's log.
 */

#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include "mio.h"
#include "mio_internal.h"
#include "mio_telemetry.h"

static struct mio_telemetry_rec_ops *mio_telem_rec_ops = NULL;
struct mio_telemetry_store mio_telemetry_streams;

int mio_telemetry_value_len(enum mio_telemetry_type type, void *value)
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

int mio_telemetry_rec_len(const char *topic,
			  enum mio_telemetry_type type,
			  void *value)
{
	int topic_len;
	int val_len;
	int buf_len;

	assert(topic != NULL);
	topic_len = strlen(topic);
	val_len = mio_telemetry_value_len(type, value);
	if (val_len < 0)
		return val_len;
	buf_len = 2 + topic_len + 1 + val_len + 1;
	return buf_len;
}

int telemetry_array_alloc(struct mio_telemetry_array *array,
			  enum mio_telemetry_type type, int nr_elms)
{
	int elm_size;

	if (array == NULL)
		return -EINVAL;

	if (type == MIO_TM_TYPE_ARRAY_UINT64)
		elm_size = sizeof(uint64_t);
	else if (type == MIO_TM_TYPE_ARRAY_UINT32)
		elm_size = sizeof(uint32_t);
	else if (type == MIO_TM_TYPE_ARRAY_UINT16)
		elm_size = sizeof(uint16_t);
	else
		return -EINVAL;
	

	array->mta_elms = malloc(elm_size * nr_elms);
	if (array->mta_elms == NULL)
		return -ENOMEM;
	array->mta_nr_elms = nr_elms;
	return 0;
}

void telemetry_array_free(struct mio_telemetry_array *array)
{
	if (array == NULL || array->mta_elms == NULL)
		return;

	free(array->mta_elms);
}

enum {
	MIO_TM_ARRAY_MAX_NR_ELMS = 32 
};

int mio_telemetry_array_advertise(char *topic,
				  enum mio_telemetry_type type,
				  int nr_elms, ...)
{
	int i;
	int rc = 0;
	va_list valist;
	uint64_t e_u64;
	uint32_t e_u32;
	uint16_t e_u16;
	uint64_t *ep_u64; /* Element Ponter*/
	uint32_t *ep_u32;
	uint16_t *ep_u16;
	struct mio_telemetry_array array;

	/* If telemetry store is NOT selected, do nothing and simply return. */
	if (mio_telem_rec_ops == NULL)
		return 0;

	if (mio_telem_rec_ops->mtro_encode == NULL ||
	    mio_telem_rec_ops->mtro_store == NULL)
		return -EOPNOTSUPP;

	if (nr_elms > MIO_TM_ARRAY_MAX_NR_ELMS)
		return -EINVAL;
	rc = telemetry_array_alloc(&array, type, nr_elms);
	if (rc < 0)
		return rc;

	/* Generate telemetry record with array value. */
	va_start(valist, nr_elms);

	for (i = 0; i < nr_elms; i++) {
		if (type == MIO_TM_TYPE_ARRAY_UINT64) {
			ep_u64 = ((uint64_t *)array.mta_elms) + i;
			e_u64 = va_arg(valist, uint64_t);
			memcpy((char *)ep_u64, (char *)&e_u64, sizeof(e_u64));
		} else if (type == MIO_TM_TYPE_ARRAY_UINT32) {
			ep_u32 = ((uint32_t *)array.mta_elms) + i;
			e_u32 = va_arg(valist, uint32_t);
			memcpy((char *)ep_u32, (char *)&e_u32, sizeof(e_u32));
		} else if (type == MIO_TM_TYPE_ARRAY_UINT16) {
			ep_u16 = ((uint16_t *)array.mta_elms) + i;
			e_u16 = va_arg(valist, int);
			memcpy((char *)ep_u16, (char *)&e_u16, sizeof(e_u16));
		} else {
			rc = -EINVAL;
			break;
		}
	}
	if (rc < 0)
		goto exit;
	mio_telemetry_advertise(topic, type, &array);

exit:
	va_end(valist);
	telemetry_array_free(&array);
	return rc;
}

int mio_telemetry_advertise(const char *topic,
			    enum mio_telemetry_type type,
			    void *value)
{
	int rc;
	int len;
	char *buf;
	struct mio_telemetry_rec rec;

	/* If telemetry store is NOT selected, do nothing and simply return. */
	if (mio_telem_rec_ops == NULL)
		return 0;

	if (mio_telem_rec_ops->mtro_encode == NULL ||
	    mio_telem_rec_ops->mtro_store == NULL)
		return -EOPNOTSUPP;

	rec.mtr_topic = topic;
	rec.mtr_type = type;
	rec.mtr_value = value;
	rc = mio_telem_rec_ops->mtro_encode(&rec, &buf, &len);
	if (rc < 0)
		return rc;

	rc = mio_telem_rec_ops->mtro_store(
		mio_telemetry_streams.mts_dump_stream, buf, len);
	mio_mem_free(buf);
	return rc;
}

int mio_telemetry_parse(struct mio_telemetry_store *sp,
			struct mio_telemetry_rec *rec)
{
	int rc;
	char *rec_buf = NULL;
	char *head = NULL;
	char *tail = NULL;

	assert(sp != NULL);

	if (mio_telem_rec_ops == NULL ||
	    mio_telem_rec_ops->mtro_load == NULL ||
	    mio_telem_rec_ops->mtro_decode == NULL)
		return -EOPNOTSUPP;

	rc = mio_telem_rec_ops->mtro_load(
		sp->mts_parse_stream, &rec_buf, &head, &tail);
	if (rc < 0)
		return rc;

	rc = mio_telem_rec_ops->mtro_decode(rec_buf, head, tail, rec);

	mio_mem_free(head);
	mio_mem_free(tail);
	mio_mem_free(rec_buf);
	return rc;
}

int mio_telemetry_init()
{
	if (mio_instance->m_telem_store_type == MIO_TM_ST_ADDB)
		mio_telem_rec_ops = &mio_motr_addb_rec_ops;
	return 0;
}

void mio_telemetry_fini()
{
	return;
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
 *
 */
