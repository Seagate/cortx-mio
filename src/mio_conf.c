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

#include "motr/client.h"

#include "logger.h"
#include "mio_internal.h"
#include "mio.h"

enum {
	YAML_MAX_KEY_LEN = 128
};

enum conf_type {
	MIO = 0,
	MOTR,
	CEPH,
};

enum conf_key {
	MIO_CONF_INVALID = -1,

	/* MIO */
	MIO_CONFIG = 0,
	MIO_LOG_LEVEL,
	MIO_LOG_FILE,
	MIO_DRIVER,

	/* Motr driver. "MOTR_CONFIG" is the key for Motr section. */
	MOTR_CONFIG,
	MOTR_INST_ADDR,
	MOTR_HA_ADDR,
	MOTR_PROFILE,
	MOTR_PROCESS_FID,
	MOTR_IS_OOSTORE,
	MOTR_IS_READ_VERIFY,
	MOTR_TM_RECV_QUEUE_MIN_LEN,
	MOTR_MAX_RPC_MSG_SIZE,
	MOTR_MAX_IOSIZE_PER_DEV,
	MOTR_DEFAULT_UNIT_SIZE,
	MOTR_USER_GROUP,
	MOTR_POOLS,
	MOTR_POOL_NAME,
	MOTR_POOL_ID,
	MOTR_POOL_TYPE,
	MOTR_POOL_DEFAULT,

	/* Other drivers such as Ceph defined here. */
};

struct conf_entry {
	char *name;
	enum conf_type type;
};

/**
 * Currently MIO put all configuration entries into one big table defined
 * below, which is not too difficult to manage considering MIO only
 * support motr driver and has limited configurations. But this will become
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

	/* Motr driver. */
	[MOTR_CONFIG] = {
		.name = "MOTR_CONFIG",
		.type = MOTR
	},
	[MOTR_INST_ADDR] = {
		.name = "MOTR_INST_ADDR",
		.type = MOTR
	},
	[MOTR_HA_ADDR] = {
		.name = "MOTR_HA_ADDR",
		.type = MOTR
	},
	[MOTR_PROFILE] = {
		.name = "MOTR_PROFILE",
		.type = MOTR
	},
	[MOTR_PROCESS_FID] = {
		.name = "MOTR_PROCESS_FID",
		.type = MOTR
	},
	[MOTR_IS_OOSTORE] = {
		.name = "MOTR_IS_OOSTORE",
		.type = MOTR
	},
	[MOTR_IS_READ_VERIFY] = {
		.name = "MOTR_IS_READ_VERIFY",
		.type = MOTR
	},
	[MOTR_TM_RECV_QUEUE_MIN_LEN] = {
		.name = "MOTR_TM_RECV_QUEUE_MIN_LEN",
		.type = MOTR
	},
	[MOTR_MAX_RPC_MSG_SIZE] = {
		.name = "MOTR_MAX_RPC_MSG_SIZE",
		.type = MOTR
	},
	[MOTR_MAX_IOSIZE_PER_DEV] = {
		.name = "MOTR_MAX_IOSIZE_PER_DEV",
		.type = MOTR
	},
	[MOTR_DEFAULT_UNIT_SIZE] = {
		.name = "MOTR_DEFAULT_UNIT_SIZE",
		.type = MOTR
	},
	[MOTR_USER_GROUP] = {
		.name = "MOTR_USER_GROUP",
		.type = MOTR
	},
	[MOTR_POOL_DEFAULT] = {
		.name = "MOTR_POOL_DEFAULT",
		.type = MOTR
	},
	[MOTR_POOLS] = {
		.name = "MOTR_POOLS",
		.type = MOTR
	},
	[MOTR_POOL_NAME] = {
		.name = "MOTR_POOL_NAME",
		.type = MOTR
	},
	[MOTR_POOL_ID] = {
		.name = "MOTR_POOL_ID",
		.type = MOTR
	},
	[MOTR_POOL_TYPE] = {
		.name = "MOTR_POOL_TYPE",
		.type = MOTR
	},
};

enum conf_block_sequence {
	CONF_BLK_SEQ_MOTR_POOLS = 1
};
static enum conf_block_sequence conf_which_blk_seq = 0;

static enum mio_driver_id mio_inst_drv_id;
static void *mio_driver_confs[MIO_DRIVER_NUM];
static struct mio_motr_config *motr_conf;

static char *mio_default_pool_null_str = "MIO_DEFAULT_POOL_NULL";
static char mio_default_pool_name[MIO_POOL_MAX_NAME_LEN + 1];
struct mio_pools mio_pools;

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
	if (!strcmp(str, "MOTR"))
		drv_id = MIO_MOTR;

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

enum {
	MIO_MOTR_DEFAULT_IOSIZE_PER_DEV = 128 * 4096
};

static int conf_alloc_driver(int key)
{
	int rc = 0;
	int type;

	/* Allocate driver's configuration structure if it hasn't been. */
	type = conf_table[key].type;
	switch(type) {
	case MOTR:
		if (motr_conf != NULL)
			break;

		motr_conf = malloc(sizeof(struct mio_motr_config));
		mio_driver_confs[MIO_MOTR] = motr_conf;
		if (motr_conf == NULL)
			rc = -ENOMEM;
		motr_conf->mc_max_iosize_per_dev =
			MIO_MOTR_DEFAULT_IOSIZE_PER_DEV;
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
	if (motr_conf) {
		mio_mem_free(motr_conf->mc_motr_local_addr);
		mio_mem_free(motr_conf->mc_ha_addr);
		mio_mem_free(motr_conf->mc_process_fid);
		mio_mem_free(motr_conf->mc_profile);
		free(motr_conf);
		motr_conf = NULL;
		mio_driver_confs[MIO_MOTR] = NULL;
	}
}

static int conf_alloc_mio_pools()
{
	mio_memset(&mio_pools, 0, sizeof(mio_pools));
	mio_pools.mps_pools = mio_mem_alloc(
		MIO_MOTR_MAX_POOL_CNT * sizeof(struct mio_pool));
	if (mio_pools.mps_pools == NULL)
		return -ENOMEM;
	else
		return 0;
}

static void conf_free_mio_pools()
{
	mio_mem_free(mio_pools.mps_pools);
	mio_memset(&mio_pools, 0, sizeof(mio_pools));
}

static int conf_pool_id_sscanf(struct mio_pool_id *pool_id, char *id_str)
{
	int rc;
	int n;
	uint64_t u1;
	uint64_t u2;

	rc = sscanf(id_str, "%"SCNx64" : %"SCNx64" %n", &u1, &u2, &n);
	if (rc < 0)
		return rc;

	pool_id->mpi_hi = u1;
	pool_id->mpi_lo = u2;
	return 0;
}

static int conf_extract_pool_type(enum mio_pool_type *ptype, char *type_str)
{
	if (type_str == NULL) {
		fprintf(stderr, "Pool type is not set!\n");
		return -EINVAL;
	}

	if (!strcmp(type_str, "NVM"))
		*ptype = MIO_POOL_TYPE_NVM;
	else if (!strcmp(type_str, "SSD"))
		*ptype = MIO_POOL_TYPE_SSD;
	else if (!strcmp(type_str, "HDD"))
		*ptype = MIO_POOL_TYPE_HDD;
	else {
		fprintf(stderr, "Unknown pool type!\n");
		return -EINVAL;
	}

	return 0;
}

static int conf_start_block()
{
	int rc = 0;
	struct mio_pool *pool;

	/* Process one block inside an array(seq) block. */
	switch(conf_which_blk_seq) {
	case CONF_BLK_SEQ_MOTR_POOLS:
		if (mio_pools.mps_nr_pools + 1 == MIO_MOTR_MAX_POOL_CNT) {
			fprintf(stderr, "Too many pools!\n");
			rc = -E2BIG;
		} else {
			pool = mio_pools.mps_pools + mio_pools.mps_nr_pools;
			mio_memset(pool, 0, sizeof(*pool));
			mio_pools.mps_nr_pools++;
		}
		break;
	default:
		/* Ignore the block. */
		break;
	}

	return rc;
}

static int conf_extract_blk_seq(enum conf_key key)
{
	int rc = 0;

	switch (key) {
	case MOTR_POOLS:
		conf_which_blk_seq = CONF_BLK_SEQ_MOTR_POOLS;
		rc = conf_alloc_mio_pools();
		break;
	default:
		break;
	}

	return rc;
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
	int slen = 0;
	struct mio_pool *pool;

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
	case MOTR_INST_ADDR:
		rc = conf_copy_str(&motr_conf->mc_motr_local_addr, value);
		break;
	case MOTR_HA_ADDR:
		rc = conf_copy_str(&motr_conf->mc_ha_addr, value);
		break;
	case MOTR_PROFILE:
		rc = conf_copy_str(&motr_conf->mc_profile, value);
		break;
	case MOTR_PROCESS_FID:
		rc = conf_copy_str(&motr_conf->mc_process_fid, value);
		break;
	case MOTR_TM_RECV_QUEUE_MIN_LEN:
		motr_conf->mc_tm_recv_queue_min_len = atoi(value);
		break;
	case MOTR_MAX_RPC_MSG_SIZE:
		motr_conf->mc_max_rpc_msg_size = atoi(value);
		break;
	case MOTR_MAX_IOSIZE_PER_DEV:
		motr_conf->mc_max_iosize_per_dev = atoi(value);
		break;
	case MOTR_DEFAULT_UNIT_SIZE:
		motr_conf->mc_unit_size = atoi(value);
		motr_conf->mc_default_layout_id =
		m0_obj_unit_size_to_layout_id(motr_conf->mc_unit_size);
		break;
	case MOTR_IS_OOSTORE:
		motr_conf->mc_is_oostore = atoi(value);
		break;
	case MOTR_IS_READ_VERIFY:
		motr_conf->mc_is_read_verify = atoi(value);
		break;
	case MOTR_USER_GROUP:
		rc = conf_copy_str(&motr_conf->mc_motr_group, value);
		break;
	case MOTR_POOL_DEFAULT:
		slen = strlen(value);
		if (slen > MIO_POOL_MAX_NAME_LEN)
			rc = -EINVAL;
		else
			memcpy(mio_default_pool_name, value, slen + 1);
		break;
	case MOTR_POOL_NAME:
		pool = mio_pools.mps_pools + mio_pools.mps_nr_pools - 1;
		slen = strlen(value);
		if (slen > MIO_POOL_MAX_NAME_LEN)
			rc = -EINVAL;
		else
			memcpy(pool->mp_name, value, slen + 1);
		break;
	case MOTR_POOL_ID:
		pool = mio_pools.mps_pools + mio_pools.mps_nr_pools - 1;
		rc = conf_pool_id_sscanf(&pool->mp_id, value);
		break;
	case MOTR_POOL_TYPE:
		pool = mio_pools.mps_pools + mio_pools.mps_nr_pools - 1;
		rc = conf_extract_pool_type(&pool->mp_type, value);
		break;
	default:
		break;
	}

	return rc;
}

bool mio_conf_default_pool_has_set()
{
	return strcmp(mio_default_pool_name, mio_default_pool_null_str);
}

int mio_conf_init(const char *config_file)
{
	int i;
	int rc;
	FILE *fh;
	bool is_key = true;
	enum conf_key key = MIO_CONF_INVALID;
	char *scalar_value;
	bool eof;
	yaml_parser_t parser;
	yaml_token_t token;

	memcpy(mio_default_pool_name,
	       mio_default_pool_null_str,
	       strlen(mio_default_pool_null_str) + 1);
	
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
		case YAML_BLOCK_MAPPING_START_TOKEN:
			break;
		case YAML_BLOCK_SEQUENCE_START_TOKEN:
			rc = conf_extract_blk_seq(key);
			break;
		case YAML_BLOCK_ENTRY_TOKEN:
			rc = conf_start_block();
			break;
		case YAML_BLOCK_END_TOKEN:
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

	/* Set default pool. */
	if (mio_conf_default_pool_has_set()) {
		for (i = 0; i < mio_pools.mps_nr_pools; i++) {
			if (!strcmp(mio_pools.mps_pools[i].mp_name,
				    mio_default_pool_name))
				break;
		}
		if (i == mio_pools.mps_nr_pools) {
			rc = -EINVAL;
			goto exit;
		}

		mio_pools.mps_default_pool_idx = i;
		memcpy(mio_pools.mps_default_pool_name,
	      	       mio_pools.mps_pools[i].mp_name,
		       MIO_POOL_MAX_NAME_LEN + 1);
	}

exit:
	yaml_parser_delete(&parser);
	fclose(fh);
	return rc;
}

void mio_conf_fini()
{
	conf_free_mio_pools();
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
