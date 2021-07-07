/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#pragma once

#ifndef __MIO_TELEMETRY_H__
#define __MIO_TELEMETRY_H__

#include <stdio.h>
#include <stdbool.h>

/**
 * Telemetry interface provides MIO and its upper applications (by applications,
 * we mean Maestro core and Maestro workflow manager etc. that uses MIO) with
 * APIs to encode/decode telmetry records and to store/load/parse records
 * to/from a storage backend.
 *
 * (1) Instead of allowing applications to self define any record
 *     structure, telemetry interface defines a set of simple and common data
 *     types, such as UINT64, STRING and ARRAY_UINT16 etc.. For a self-defined
 *     record structure, applications will have to also generate a description
 *     of this data structure (such as those defined by protobuf or json) and
 *     pass to MIO telemetry in order to encode and decode the structure.
 *     This adds burden for applications and also complicates the interactions
 *     between MIO and its applications.
 *
 *     Using Common data types removes the need to generate record structure
 *     description although it may be more inconvenient to output a complex
 *     data structure. But common data types defined in this interface should
 *     be suffice for most of cases.
 *
 * (2) Based on the common record types, a telemetry record is defined as a
 *     tuple {topic, record type, value}
 *
 *     `topic` is defined as a string.
 *
 * (3) Telemetry API is defined as below:
 *
 *     int mio_telemetry_advertise(const char *topic,
 *     		  	           enum mio_telemetry_data_type, void *data);
 *
 *     The API to parse a telemetry record is defined as:
 *
 *     int mio_telemetry_parse(struct mio_telemetry_store *sp,
 *     			       struct mio_telemetry_rec *rec);
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

enum mio_telemetry_store_type {
	MIO_TM_ST_INVALID = -1,
	MIO_TM_ST_NONE,            /* Default setting is to turn telemetry off. */
	MIO_TM_ST_LOG,
	MIO_TM_ST_ADDB,
};

struct mio_telemetry_store {
	void *mts_dump_stream;
	void *mts_parse_stream;
};

extern struct mio_telemetry_store mio_telem_streams;

/**
 * Timespan and timepoint are defined as uint64_t.
 *
 * For any array, the first element stores the number of
 * elements in the array.
 */
enum mio_telemetry_type {
	MIO_TM_TYPE_INVALID = 0,
	MIO_TM_TYPE_NONE, /* No value attached to the data point. */
	MIO_TM_TYPE_UINT16,
	MIO_TM_TYPE_UINT32,
	MIO_TM_TYPE_UINT64,
	MIO_TM_TYPE_TIMESPAN,
	MIO_TM_TYPE_TIMEPOINT,
	MIO_TM_TYPE_STRING,
	MIO_TM_TYPE_ARRAY_UINT16,
	MIO_TM_TYPE_ARRAY_UINT32,
	MIO_TM_TYPE_ARRAY_UINT64,
	MIO_TM_TYPE_NR
};

struct mio_telemetry_array {
	int mta_nr_elms;
	void *mta_elms;
};

struct mio_telemetry_rec {
	char *mtr_time_str; /* Time in string. */
	char *mtr_prefix;
	const char *mtr_topic;
	enum mio_telemetry_type mtr_type;
	void *mtr_value;
};

struct mio_telemetry_rec_ops {
	int (*mtro_encode) (const struct mio_telemetry_rec *rec,
			    char **buf, int *len);
	int (*mtro_decode) (const char *rec_buf, const char *head,
			    const char *tail, struct mio_telemetry_rec *rec);

	int (*mtro_store) (void *dump_stream, const char *rec_buf, int len);
	int (*mtro_load) (void *parse_stream, char **rec_buf,
			  char **head, char **tail);
	
};

struct mio_telemetry_conf {
	enum mio_telemetry_store_type mtc_type;
	/* Prefix added to the beginning of a telemetry record. */
	char *mtc_prefix;
	bool mtc_is_parser;
	void *mtc_store_conf;
};
extern struct mio_telemetry_conf mio_telem_conf;

extern struct mio_telemetry_rec_ops mio_motr_addb_rec_ops;
extern struct mio_telemetry_rec_ops mio_telem_log_rec_ops;

/**
 * MIO telemetry APIs.
 *
 * (1) Initialise and finalise telemetry subsystem.
 * mio_telemetry_init() and mio_telemetry_fini().
 *
 * @param conf Telemetry configuration parameters, including the type
 * of telemetry store and store specific parameters. For example, if
 * MIO_TM_ST_LOG is selected as telemetry backend store. The caller
 * sets the mio_telemetry_conf::mtc_store_conf to the directory of log
 * files.
 * @return = 0 for success, anything else for an error.
 *
 * (2) Output and store telemetry a record: mio_telemetry_advertise()
 * and mio_telemetry_array_advertise()
 * mio_telemetry_array_advertise() is a wrapper function of
 * mio_telemetry_advertise() for an array of data.
 *
 * @param topic. The `topic` string is used to identify where the
 * record is created and its purpose.
 * @param type Data value type, see `enum mio_telemetry_type` for details.
 * @param value Pointer to data value.
 * @return = 0 for success, anything else for an error.
 *
 * (3) Parse a record.
 * mio_telemetry_parse()
 *
 * @param sp. Pointer to a telemetry store. For a parser, set
 * mio_telemetry_store::mts_parse_stream to the input telemetry stream.
 * For example, the parse stream for log is set to the file stream of
 * the log file.
 * @param rec[out] The pointer to the decoded record.   
 * @return = 0 for success, anything else for an error.
 */
int mio_telemetry_advertise(const char *topic,
			    enum mio_telemetry_type type,
			    void *value);
int mio_telemetry_array_advertise(const char *topic,
				  enum mio_telemetry_type type,
				  int nr_elms, ...);
int mio_telemetry_parse(struct mio_telemetry_store *sp,
			struct mio_telemetry_rec *rec);

int mio_telemetry_init(struct mio_telemetry_conf *conf);
void mio_telemetry_fini();

/** Some internal APIs :). */
int mio_telemetry_advertise_noprefix(const char *topic,
				     enum mio_telemetry_type type,
				     void *value);
int mio_telemetry_array_advertise_noprefix(const char *topic,
					   enum mio_telemetry_type type,
					   int nr_elms, ...);

/** Helper functions. */
int mio_telemetry_alloc_value(enum mio_telemetry_type type, void **value);

#endif

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
