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
#include <string.h>
#include <assert.h>

#include "logger.h"
#include "utils.h"
#include "mio_telemetry.h"

#ifdef MIO_MOTR_ADDB
#include "mio.h"
#include "mio_internal.h"
#endif

static struct mio_telemetry_rec_ops *mio_telem_rec_ops = NULL;
struct mio_telemetry_store mio_telem_streams;
struct mio_telemetry_conf mio_telem_conf;

int mio_telemetry_alloc_value(enum mio_telemetry_type type, void **value)
{
	int rc = 0;
	int size;

	*value = NULL;
	if (type == MIO_TM_TYPE_NONE)
		return 0;

	switch (type) {
	case MIO_TM_TYPE_UINT16:
		size = sizeof(uint16_t);
		break;
	case MIO_TM_TYPE_UINT32:
		size = sizeof(uint32_t);
		break;
	case MIO_TM_TYPE_UINT64:
	case MIO_TM_TYPE_TIMESPAN:
	case MIO_TM_TYPE_TIMEPOINT:
		size = sizeof(uint64_t);
		break;
	case MIO_TM_TYPE_ARRAY_UINT16:
	case MIO_TM_TYPE_ARRAY_UINT32:
	case MIO_TM_TYPE_ARRAY_UINT64:
		size = sizeof(struct mio_telemetry_array);
		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc < 0)
		return rc;

	*value = mio_mem_alloc(size);
	if (*value == NULL)
		return -ENOMEM;
	else
		return 0;
}


static int telemetry_array_alloc(struct mio_telemetry_array *array,
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
	

	array->mta_elms = mio_mem_alloc(elm_size * nr_elms);
	if (array->mta_elms == NULL)
		return -ENOMEM;
	array->mta_nr_elms = nr_elms;
	return 0;
}

static void telemetry_array_free(struct mio_telemetry_array *array)
{
	if (array == NULL || array->mta_elms == NULL)
		return;

	mio_mem_free(array->mta_elms);
}

enum {
	MIO_TM_ARRAY_MAX_NR_ELMS = 32 
};

static int telemetry_do_advertise(bool prefix, const char *topic,
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

	rec.mtr_prefix = prefix == true? mio_telem_conf.mtc_prefix : NULL;
	rec.mtr_topic = topic;
	rec.mtr_type = type;
	rec.mtr_value = value;
	rc = mio_telem_rec_ops->mtro_encode(&rec, &buf, &len);
	if (rc < 0)
		return rc;

	rc = mio_telem_rec_ops->mtro_store(
		mio_telem_streams.mts_dump_stream, buf, len);
	mio_mem_free(buf);
	return rc;
}

int mio_telemetry_advertise(const char *topic,
			    enum mio_telemetry_type type,
			    void *value)
{
	return telemetry_do_advertise(true, topic, type, value);
}

int mio_telemetry_advertise_noprefix(const char *topic,
				     enum mio_telemetry_type type,
				     void *value)
{
	return telemetry_do_advertise(false, topic, type, value);
}

static int telemetry_do_array_advertise(bool prefix, const char *topic,
					enum mio_telemetry_type type,
					int nr_elms, va_list valist)
{
	int i;
	int rc = 0;
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
	for (i = 0; i < nr_elms; i++) {
		if (type == MIO_TM_TYPE_ARRAY_UINT64) {
			ep_u64 = ((uint64_t *)array.mta_elms) + i;
			e_u64 = va_arg(valist, uint64_t);
			mio_mem_copy((char *)ep_u64, (char *)&e_u64,
				     sizeof(e_u64));
		} else if (type == MIO_TM_TYPE_ARRAY_UINT32) {
			ep_u32 = ((uint32_t *)array.mta_elms) + i;
			e_u32 = va_arg(valist, uint32_t);
			mio_mem_copy((char *)ep_u32, (char *)&e_u32,
				     sizeof(e_u32));
		} else if (type == MIO_TM_TYPE_ARRAY_UINT16) {
			ep_u16 = ((uint16_t *)array.mta_elms) + i;
			e_u16 = va_arg(valist, int);
			mio_mem_copy((char *)ep_u16, (char *)&e_u16,
				     sizeof(e_u16));
		} else {
			rc = -EINVAL;
			break;
		}
	}
	if (rc < 0)
		goto exit;
	telemetry_do_advertise(prefix, topic, type, &array);

exit:
	telemetry_array_free(&array);
	return rc;
}

int mio_telemetry_array_advertise(const char *topic,
				  enum mio_telemetry_type type,
				  int nr_elms, ...)
{
	int rc;
	va_list valist;

	va_start(valist, nr_elms);	
	rc = telemetry_do_array_advertise(true, topic, type, nr_elms, valist);
	va_end(valist);
	return rc;
}

int mio_telemetry_array_advertise_noprefix(const char *topic,
					   enum mio_telemetry_type type,
					   int nr_elms, ...)
{
	int rc;
	va_list valist;

	va_start(valist, nr_elms);	
	rc = telemetry_do_array_advertise(false, topic, type, nr_elms, valist);
	va_end(valist);
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

static void telemetry_conf_free()
{
	mio_mem_free(mio_telem_conf.mtc_prefix);
	mio_mem_free(mio_telem_conf.mtc_store_conf);
}

static int telemetry_conf_save(struct mio_telemetry_conf *conf)
{
	int rc = 0;
	int len = 0;
	enum mio_telemetry_store_type type = conf->mtc_type;
	char *mio_log_dir;

	mio_memset(&mio_telem_conf, 0, sizeof(mio_telem_conf));

	/* Save prefix. */
	if (conf->mtc_prefix != NULL) {
		len = strlen(conf->mtc_prefix) + 1;
		mio_telem_conf.mtc_prefix = mio_mem_alloc(len);
		if (mio_telem_conf.mtc_prefix == NULL)
			return -ENOMEM;
		mio_mem_copy(mio_telem_conf.mtc_prefix, conf->mtc_prefix, len);
	}

	/* Save store specific configurations. */
	switch (type) {
	case MIO_TM_ST_NONE:
		break;
#ifdef MIO_MOTR_ADDB
	case MIO_TM_ST_ADDB:
		break;
#endif
	case MIO_TM_ST_LOG:
		mio_log_dir = (char *)conf->mtc_store_conf;
		if (mio_log_dir != NULL) {
			len = strnlen(mio_log_dir, MIO_LOG_DIR_PATH_MAX) + 1;
			mio_telem_conf.mtc_store_conf = mio_mem_alloc(len);
			if (mio_telem_conf.mtc_store_conf == NULL)
				rc = -ENOMEM;
			else
				mio_mem_copy(mio_telem_conf.mtc_store_conf,
					     mio_log_dir, len);
		}
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}

	if (rc != 0)
		telemetry_conf_free();
	return rc;
}

int mio_telemetry_init(struct mio_telemetry_conf *conf)
{
	int rc = 0;
	enum mio_telemetry_store_type type = conf->mtc_type;
	char *mio_log_dir;

	rc = telemetry_conf_save(conf);
	if (rc < 0)
		return rc;

	switch (type) {
	case MIO_TM_ST_NONE:
		mio_telem_rec_ops = NULL;
		break;
#ifdef MIO_MOTR_ADDB
	case MIO_TM_ST_ADDB:
		if (!conf->mtc_is_parser) {
			rc = mio_instance_check();
			if (rc < 0)
				return rc;
			if (mio_instance->m_driver_id != MIO_MOTR)
				return -EINVAL;
		}
		mio_telem_rec_ops = &mio_motr_addb_rec_ops;
		break;
#endif
	case MIO_TM_ST_LOG:
		if (!conf->mtc_is_parser && mio_log_file == NULL) {
			mio_log_dir = (char *)conf->mtc_store_conf;
			rc = mio_log_init(MIO_TELEMETRY, mio_log_dir);
			if (rc < 0)
				return rc;
		}
		mio_telem_rec_ops = &mio_telem_log_rec_ops;
		break;
	default:
		rc = -EOPNOTSUPP;
		break;
	}


	return rc;
}

void mio_telemetry_fini()
{
	telemetry_conf_free();
	mio_telem_rec_ops = NULL;
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
