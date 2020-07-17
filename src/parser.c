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
#include <stdbool.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <yaml.h>

#include "clovis/clovis.h"

#include "logger.h"
#include "mio_internal.h"
#include "mio.h"

enum {
	YAML_MAX_KEY_LEN = 128
};

enum conf_type {
	MIO = 0,
	MERO,
	CEPH,
};

enum conf_key {
	MIO_CONF_INVALID = -1,

	/* MIO */
	MIO_CONFIG = 0,
	MIO_LOG_LEVEL,
	MIO_LOG_FILE,
	MIO_DRIVER,

	/* Mero driver. "MERO_CONFIG" is the key for Mero section. */
	MERO_CONFIG,
	MERO_CLOVIS_INST_ADDR,
	MERO_HA_ADDR,
	MERO_PROFILE,
	MERO_PROCESS_FID,
	MERO_IS_OOSTORE,
	MERO_IS_READ_VERIFY,
	MERO_TM_RECV_QUEUE_MIN_LEN,
	MERO_MAX_RPC_MSG_SIZE,
	MERO_DEFAULT_UNIT_SIZE,
	MERO_USER_GROUP,

	/* Other drivers such as Ceph defined here. */
};

struct conf_entry {
	char *name;
	enum conf_type type;
};

/**
 * Currently MIO put all configuration entries into one big table defined
 * below, which is not too difficult to manage considering MIO only
 * support Mero driver and has limited configurations. But this will become
 * a problem when more drivers are implemented. A better solution is to
 * seperate the big table into driver specific ones and to define driver's
 * configuration operations to parse its entries. 
 *
 * TODO: driver specific configuration table and operations.
 */
struct conf_entry conf_table[] = {
	/* MIO */
	[MIO_CONFIG] = {
		.name = "MIO_CONFIG",
		.type = MIO
	},
	[MIO_LOG_FILE] = {
		.name = "MIO_LOG_FILE",
		.type = MIO
	},
	[MIO_LOG_LEVEL] = {
		.name = "MIO_LOG_LEVEL",
		.type = MIO
	},
	[MIO_DRIVER] = {
		.name = "MIO_DRIVER",
		.type = MIO
	},

	/* Mero driver. */
	[MERO_CONFIG] = {
		.name = "MERO_CONFIG",
		.type = MERO
	},
	[MERO_CLOVIS_INST_ADDR] = {
		.name = "MERO_CLOVIS_INST_ADDR",
		.type = MERO
	},
	[MERO_HA_ADDR] = {
		.name = "MERO_HA_ADDR",
		.type = MERO
	},
	[MERO_PROFILE] = {
		.name = "MERO_PROFILE",
		.type = MERO
	},
	[MERO_PROCESS_FID] = {
		.name = "MERO_PROCESS_FID",
		.type = MERO
	},
	[MERO_IS_OOSTORE] = {
		.name = "MERO_IS_OOSTORE",
		.type = MERO
	},
	[MERO_IS_READ_VERIFY] = {
		.name = "MERO_IS_READ_VERIFY",
		.type = MERO
	},
	[MERO_TM_RECV_QUEUE_MIN_LEN] = {
		.name = "MERO_TM_RECV_QUEUE_MIN_LEN",
		.type = MERO
	},
	[MERO_MAX_RPC_MSG_SIZE] = {
		.name = "MERO_MAX_RPC_MSG_SIZE",
		.type = MERO
	},
	[MERO_DEFAULT_UNIT_SIZE] = {
		.name = "MERO_DEFAULT_UNIT_SIZE",
		.type = MERO
	},
	[MERO_USER_GROUP] = {
		.name = "MERO_USER_GROUP",
		.type = MERO
	},

};

static enum mio_driver_id mio_inst_drv_id;
static void *mio_driver_confs[MIO_DRIVER_NUM];
static struct mio_mero_config *mero_conf;

#define NKEYS (sizeof(conf_table)/sizeof(struct conf_entry))

static int conf_token_to_key(char *token, enum conf_key *key)
{
	int rc = 0;
	int i;
	char *s1;

	for(i = 0; i < NKEYS; i++) {
		s1 = strstr(token, conf_table[i].name);
		if (s1 != NULL && !strcmp(s1, conf_table[i].name)) {
			*key = i;
			break;
		}
	}
	if (i == NKEYS)
		rc = MIO_CONF_INVALID;
	return rc;
}

static enum mio_driver_id conf_get_driver_id(const char *str)
{
	enum mio_driver_id drv_id = MIO_DRIVER_INVALID;

	assert(str != NULL);
	if (!strcmp(str, "MERO"))
		drv_id = MIO_MERO;

	return drv_id;	
}

static enum mio_log_level conf_get_log_level(const char *str)
{
	assert(str != NULL);
	if (!strcmp(str, "MIO_ERROR"))
		return MIO_ERROR;
	else if (!strcmp(str, "MIO_WARN"))
		return MIO_WARN;
	else if (!strcmp(str, "MIO_INFO"))
		return MIO_INFO;
	else if (!strcmp(str, "MIO_TRACE"))
		return MIO_TRACE;
	else if (!strcmp(str, "MIO_DEBUG"))
		return MIO_DEBUG;
	else
		return MIO_LOG_INVALID;
}

static int conf_copy_str(char **to, char *from)
{
	int len = strlen(from) + 1;

	*to = mio_mem_alloc(len);
	if (*to == NULL)
		return  -ENOMEM;

	strncpy(*to, from, len);
	return 0;
}

static int conf_alloc_driver(int key)
{
	int rc = 0;
	int type;

	/* Allocate driver's configuration structure if it hasn't been. */
	type = conf_table[key].type;
	switch(type) {
	case MERO:
		if (mero_conf != NULL)
			break;

		mero_conf = malloc(sizeof(struct mio_mero_config));
		mio_driver_confs[MIO_MERO] = mero_conf;
		if (mero_conf == NULL)
			rc = -ENOMEM;
		break;
	case CEPH:
		fprintf(stderr, "Ceph driver is not supported yet!");
		rc = -EINVAL;
		break;
	default:
		break;
	}

	return rc;
}

static void conf_free_drivers()
{
	if (mero_conf) {
		mio_mem_free(mero_conf->mc_clovis_local_addr);
		mio_mem_free(mero_conf->mc_ha_addr);
		mio_mem_free(mero_conf->mc_process_fid);
		mio_mem_free(mero_conf->mc_profile);
		free(mero_conf);
		mero_conf = NULL;
		mio_driver_confs[MIO_MERO] = NULL;
	}
}

static int conf_extract_key(enum conf_key *key, char *token)
{
	int rc;

	rc = conf_token_to_key(token, key);
	if (rc < 0) {
		fprintf(stderr, "Invalid token name: %s\n", token);
		return rc;
	}

	rc = conf_alloc_driver(*key);
	if (rc < 0)
		fprintf(stderr, "Can't allocate memory for driver!");

	return rc;
}

static int conf_extract_value(enum conf_key key, char *value)
{
	int rc = 0;

	/* Extract configuration value. */
	switch(key) {
	case MIO_DRIVER:
		mio_inst_drv_id = conf_get_driver_id(value); 
		if (mio_inst_drv_id == MIO_DRIVER_INVALID)
			rc = -EINVAL;
		break;
	case MIO_LOG_LEVEL:
		assert(mio_instance != NULL);
		mio_instance->m_log_level = conf_get_log_level(value);
		if (mio_instance->m_log_level == MIO_LOG_INVALID)
			rc = -EINVAL;
		break;
	case MIO_LOG_FILE:
		assert(mio_instance != NULL && value != NULL);
		rc = conf_copy_str(&mio_instance->m_log_file, value);
		break;
	case MERO_CLOVIS_INST_ADDR:
		rc = conf_copy_str(&mero_conf->mc_clovis_local_addr, value);
		break;
	case MERO_HA_ADDR:
		rc = conf_copy_str(&mero_conf->mc_ha_addr, value);
		break;
	case MERO_PROFILE:
		rc = conf_copy_str(&mero_conf->mc_profile, value);
		break;
	case MERO_PROCESS_FID:
		rc = conf_copy_str(&mero_conf->mc_process_fid, value);
		break;
	case MERO_TM_RECV_QUEUE_MIN_LEN:
		mero_conf->mc_tm_recv_queue_min_len = atoi(value);
		break;
	case MERO_MAX_RPC_MSG_SIZE:
		mero_conf->mc_max_rpc_msg_size = atoi(value);
		break;
	case MERO_DEFAULT_UNIT_SIZE:
		mero_conf->mc_unit_size = atoi(value);
		mero_conf->mc_default_layout_id =
		  m0_clovis_obj_unit_size_to_layout_id(mero_conf->mc_unit_size);
		break;
	case MERO_IS_OOSTORE:
		mero_conf->mc_is_oostore = atoi(value);
		break;
	case MERO_IS_READ_VERIFY:
		mero_conf->mc_is_read_verify = atoi(value);
		break;
	case MERO_USER_GROUP:
		rc = conf_copy_str(&mero_conf->mc_mero_group, value);
		break;
	default:
		break;
	}

	return rc;
}

int mio_conf_init(const char *config_file)
{
	int  rc;
	FILE *fh;
	bool is_key = true;
	enum conf_key key = MIO_CONF_INVALID;
	char *scalar_value;
	bool eof;
	yaml_parser_t parser;
	yaml_token_t token;

	if (!yaml_parser_initialize(&parser)) {
		/* MIO logger has not been initialised yet, so use fprintf. */
		fprintf(stderr, "Failed to initialize parser!\n");
		return -1;
	}

	fh = fopen(config_file, "r");
	if (fh == NULL) {
		fprintf(stderr, "Failed to open file!\n");
		return -1;
	}

	/* Scan the yaml file and retrieve configuration values. */
	rc = 0;
	eof = false;
	yaml_parser_set_input_file(&parser, fh);

	while(!eof) {
		yaml_parser_scan(&parser, &token);

		switch (token.type) {
		case YAML_KEY_TOKEN:
			is_key = true;
			break;
		case YAML_VALUE_TOKEN:
			is_key = false;
			break;
		case YAML_SCALAR_TOKEN:
			scalar_value = (char *)token.data.scalar.value;
			if (is_key)
				rc = conf_extract_key(&key, scalar_value);
			else
				rc = conf_extract_value(key, scalar_value);
			break;
		case YAML_STREAM_END_TOKEN:				
			eof = true;
			break;
		default:
			break;
		}

		yaml_token_delete(&token);

		if (rc < 0)
			break;
	}
	if (rc < 0) {
		conf_free_drivers();
		goto exit;
	}

	/* Set driver properly. */	
	mio_instance->m_driver_id = mio_inst_drv_id;
	mio_instance->m_driver = mio_driver_get(mio_inst_drv_id);
	mio_instance->m_driver_confs = mio_driver_confs[mio_inst_drv_id];

exit:
	yaml_parser_delete(&parser);
	fclose(fh);
	return rc;
}

void mio_conf_fini()
{
	conf_free_drivers();
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
