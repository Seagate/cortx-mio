/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <errno.h>
#include <assert.h>

#include "logger.h"
#include "utils.h"
#include "mio.h"
#include "mio_internal.h"

enum {
	MIO_HINT_INVALID = -1
};

struct hint {
	char *h_name;
	enum mio_hint_type h_type;
};

static struct hint obj_hint_table[] = {
	[MIO_HINT_OBJ_LIFETIME] = {
		.h_name = "MIO_HINT_OBJ_LIFETIME",
		.h_type = MIO_HINT_PERSISTENT,
	},
	[MIO_HINT_OBJ_WHERE] = {
		.h_name = "MIO_HINT_OBJ_WHERE",
		.h_type = MIO_HINT_SESSION,
	},
	[MIO_HINT_OBJ_HOT_INDEX] = {
		.h_name = "MIO_HINT_OBJ_HOT_INDEX",
		.h_type = MIO_HINT_PERSISTENT,
	}
};

static struct hint sys_hint_table[] = {
	[MIO_HINT_HOT_OBJ_THRESHOLD] = {
		.h_name = "MIO_HINT_HOT_OBJ_THRESHOLD",
		.h_type = MIO_HINT_SESSION,
	},
	[MIO_HINT_COLD_OBJ_THRESHOLD] = {
		.h_name = "MIO_HINT_COLD_OBJ_THRESHOLD",
		.h_type = MIO_HINT_SESSION,
	},
};

struct mio_hints mio_sys_hints;

int mio_hint_map_init(struct mio_hint_map *map, int nr_entries)
{
	int i;

	assert(map != NULL && nr_entries > 0);
	map->mhm_nr_entries = nr_entries;

	map->mhm_keys = mio_mem_alloc(nr_entries * sizeof(int));
	map->mhm_values = mio_mem_alloc(nr_entries * sizeof(uint64_t));
	if (map->mhm_keys == NULL ||
	    map->mhm_values == NULL) {
		mio_log(MIO_ERROR, "Can't create map !");
		mio_hint_map_fini(map);
		return -ENOMEM;
	}

	for (i = 0; i < nr_entries; i++) {
		map->mhm_keys[i] = MIO_HINT_INVALID;
		map->mhm_values[i] = 0;
	}
	map->mhm_nr_set = 0;

	return 0;
}

void mio_hint_map_fini(struct mio_hint_map *map)
{
	mio_mem_free(map->mhm_keys);
	mio_mem_free(map->mhm_values);
	map->mhm_nr_entries = 0;
	map->mhm_nr_set = 0;
}

int mio_hint_map_copy(struct mio_hint_map *to, struct mio_hint_map *from)
{
	int i;
	int j;
	int rc;
	int hkey;
	uint64_t hvalue;
	int nr_new = 0;

	assert(to != NULL && from != NULL);

	if (to->mhm_nr_entries == 0 ) {
		rc = mio_hint_map_init(to, from->mhm_nr_entries);
		if (rc < 0) {
			mio_log(MIO_ERROR, "Failed to initialise hint map!");
			return rc;
		}
	}

	/* Check if 'to' hint map has enough space for new hints. */
	for (i = 0; i < from->mhm_nr_set; i++) {
		hkey = from->mhm_keys[i];
		for (j = 0; j < to->mhm_nr_set; j++)
			if (to->mhm_keys[j] == hkey)
				break;
		if (j == to->mhm_nr_set)
			nr_new++;
	}
	if (to->mhm_nr_set + nr_new > to->mhm_nr_entries)
		return -E2BIG;

	/* It is safe to add new hints now. */
	for (i = 0; i < from->mhm_nr_set; i++) {
		hkey = from->mhm_keys[i];
		hvalue = from->mhm_values[i];
		for (j = 0; j < to->mhm_nr_set; j++)
			if (to->mhm_keys[j] == hkey)
				break;
		if (j == to->mhm_nr_set) {
			j = to->mhm_nr_set;
			to->mhm_nr_set++;
		}

		to->mhm_keys[j] = hkey;
		to->mhm_values[j] = hvalue;
	}
	return 0;
}

int mio_hint_map_set(struct mio_hint_map *map, int key, uint64_t value)
{
	int i;

	for (i = 0; i < map->mhm_nr_set; i++) {
		if (map->mhm_keys[i] == key)
			break;
	}
	if (i == map->mhm_nr_set) {
		if (map->mhm_nr_set == map->mhm_nr_entries)
			return -EINVAL;
		else
			map->mhm_nr_set++;
	}

	map->mhm_keys[i] = key;
	map->mhm_values[i] = value;
	return 0;
}

int mio_hint_map_get(struct mio_hint_map *map, int key, uint64_t *value)
{
	int i;

	assert(map != NULL && value != NULL);

	for (i = 0; i < map->mhm_nr_set; i++) {
		if (map->mhm_keys[i] == key)
			break;
	}
	if (i == map->mhm_nr_set)
		return -ENOENT;
	*value = map->mhm_values[i];
	return 0;
}

#define OBJ_NKEYS (sizeof(obj_hint_table)/sizeof(struct hint))
#define SYS_NKEYS (sizeof(sys_hint_table)/sizeof(struct hint))
enum mio_hint_type mio_hint_type(enum mio_hint_scope scope, int key)
{
	 enum mio_hint_type type = -EINVAL;

	switch(scope) {
	case MIO_HINT_SCOPE_OBJ:
		if (key >=0 && key < OBJ_NKEYS)
			type = obj_hint_table[key].h_type;
		break;
	case MIO_HINT_SCOPE_SYS:
		if (key >= 0 && key < SYS_NKEYS)
			type = sys_hint_table[key].h_type;
		break;
	default:
		mio_log(MIO_ERROR, "Unsupported hint scope!");
		break;
	}

	return type;
}

char* mio_hint_name(enum mio_hint_scope scope, int key)
{
	char *name = NULL;

	switch(scope) {
	case MIO_HINT_SCOPE_OBJ:
		if (key >= 0 && key < OBJ_NKEYS)
			name = obj_hint_table[key].h_name;
		break;
	case MIO_HINT_SCOPE_SYS:
		if (key >= 0 && key < SYS_NKEYS)
			name = sys_hint_table[key].h_name;
		break;
	default:
		mio_log(MIO_ERROR, "Unsupported hint scope!");
		break;
	}

	return name;
}

int mio_hints_init(struct mio_hints *hints)
{
	if (hints == NULL)
		return -EINVAL;
	return mio_hint_map_init(&hints->mh_map, MIO_OBJ_HINT_NUM);
}

void mio_hints_fini(struct mio_hints *hints)
{
	if (hints == NULL)
		return;
	mio_hint_map_fini(&hints->mh_map);
}

#define drv_obj_ops (mio_instance->m_driver->md_obj_ops)
static int obj_hint_store(struct mio_obj *obj)
{
	int i;
	int rc;
	int nr_hints;
	int nr_phints = 0;
	int phint_cnt = 0;
	struct mio_hints *phints = &obj->mo_attrs.moa_phints;

	if (obj == NULL)
		return -EINVAL;

	nr_hints = obj->mo_hints.mh_map.mhm_nr_set;

	for (i = 0; i < nr_hints; i++)
		if (mio_hint_type(MIO_HINT_SCOPE_OBJ,
				  obj->mo_hints.mh_map.mhm_keys[i]) ==
		    MIO_HINT_PERSISTENT)
			nr_phints++;

	if (phints->mh_map.mhm_nr_entries != 0)
		mio_hint_map_fini(&phints->mh_map);
	rc = mio_hint_map_init(&phints->mh_map, MIO_OBJ_HINT_NUM);
	if (rc < 0)
		return rc;

	for (i = 0; i < nr_hints; i++) {
		if (mio_hint_type(MIO_HINT_SCOPE_OBJ,
				  obj->mo_hints.mh_map.mhm_keys[i]) ==
		    MIO_HINT_PERSISTENT) {
			phints->mh_map.mhm_keys[phint_cnt] =
				obj->mo_hints.mh_map.mhm_keys[i];
			phints->mh_map.mhm_values[phint_cnt] =
				obj->mo_hints.mh_map.mhm_values[i];
			phint_cnt++;
		}
	}
	phints->mh_map.mhm_nr_set = phint_cnt;

	return drv_obj_ops->moo_hint_store(obj);
}

static int obj_hint_load(struct mio_obj *obj)
{
	if (obj == NULL)
		return -EINVAL;
	return drv_obj_ops->moo_hint_load(obj);
}

static int obj_hot_index_cal(struct mio_obj *obj)
{
	int rc;
	uint64_t hotness;

	/*
	 * A simple algorithm which only considers the number of accesses
	 * to an object. (Proof of Concept only)
	 *
	 * More sophisticated algorithm taking account of object aging
	 * and other factors (such as data location etc.) will be
	 * designed later.
	 */
	hotness = (obj->mo_attrs.moa_stats.mos_rcount +
		   obj->mo_attrs.moa_stats.mos_wcount);
	mio_log(MIO_DEBUG, "object hotness = %lu\n", hotness);
	rc = mio_hint_map_set(&obj->mo_hints.mh_map, MIO_HINT_OBJ_HOT_INDEX, hotness);
	return rc;
}

static int obj_dyn_hint_update(struct mio_obj *obj, int hint_key)
{
	int rc = 0;

	switch(hint_key) {
	case MIO_HINT_OBJ_HOT_INDEX:
		rc = obj_hot_index_cal(obj);
		break;
	default:
		break;
	}

	return rc;
}

static int mio_obj_hint_ops_check()
{
	int rc;

	rc = mio_instance_check();
	if (rc < 0)
		return rc;

	if (drv_obj_ops == NULL ||
	    drv_obj_ops->moo_hint_load == NULL ||
	    drv_obj_ops->moo_hint_store == NULL)
		return -EOPNOTSUPP;

	return 0;
}

int mio_hint_add(struct mio_hints *hints,
		 int hint_key, uint64_t hint_value)
{
	int rc;

	if (hints == NULL)
		return -EINVAL;
	/* Overwrite the hint's value if it has already been set before. */
	rc = mio_hint_map_set(&hints->mh_map, hint_key, hint_value);
	return rc;
}

int mio_hint_lookup(struct mio_hints *hints,
		    int hint_key, uint64_t *hint_value)
{
	int rc;

	if (hints == NULL)
		return -EINVAL;
	rc = mio_hint_map_get(&hints->mh_map, hint_key, hint_value);
	return rc;
}

bool mio_hint_is_set(struct mio_hints *hints, int hint_key)
{
	int rc;
	uint64_t hint_value;

	if (hints == NULL)
		return false;

	rc = mio_hint_lookup(hints, hint_key, &hint_value);
	if (rc < 0)
		return false;
	if (hint_value == MIO_HINT_VALUE_NULL)
		return false;

	return true;
}

/**
 * MIO hint API to set and get all hints of an object.
 */
int mio_obj_hints_set(struct mio_obj *obj, struct mio_hints *hints)
{
	int rc;

	rc = mio_obj_hint_ops_check();
	if (rc < 0)
		return rc;
	if (obj == NULL || hints == NULL)
		return -EINVAL;

	rc = mio_hint_map_copy(&obj->mo_hints.mh_map, &hints->mh_map)? :
	     obj_hint_store(obj);
	if (rc < 0) {
		mio_log(MIO_ERROR,
			"Copying and store hints failed! error = %d\n", rc);
		return rc;
	}

	return 0;
}

int mio_obj_hints_get(struct mio_obj *obj, struct mio_hints *hints)
{
	int rc;

	rc = mio_obj_hint_ops_check();
	if (rc < 0)
		return rc;
	if (obj == NULL || hints == NULL)
		return -EINVAL;

	rc = obj_hint_load(obj);
	if (rc < 0)
		return rc;

	rc = mio_hint_map_copy(&hints->mh_map, &obj->mo_hints.mh_map);
	return rc;
}

/**
 * MIO hint API to set and get one single hint of an object.
 */
int mio_obj_hint_set(struct mio_obj *obj, int hint_key, uint64_t hint_value)
{
	int rc;

	rc = mio_obj_hint_ops_check();
	if (rc < 0)
		return rc;
	if (obj == NULL)
		return -EINVAL;

	rc = mio_hint_add(&obj->mo_hints, hint_key, hint_value)? :
	     obj_hint_store(obj);
	if (rc < 0) {
		mio_log(MIO_ERROR,
			"Copying and store hints failed! error = %d\n", rc);
		return rc;
	}

	return 0;
}

int mio_obj_hint_get(struct mio_obj *obj, int hint_key, uint64_t *hint_value)
{
	int rc;

	rc = mio_obj_hint_ops_check();
	if (rc < 0)
		return rc;
	if (obj == NULL)
		return -EINVAL;

	rc = obj_hint_load(obj);
	if (rc < 0)
		return rc;

	/* Dynamic hint's value is calculated when it's queried. */
	rc = obj_dyn_hint_update(obj, hint_key);
	if (rc < 0)
		return rc;

	rc = mio_hint_lookup(&obj->mo_hints, hint_key, hint_value);
	return rc;
}

enum {
       MIO_DEFAULT_COLD_OBJ_THLD = 16,
       MIO_DEFAULT_HOT_OBJ_THLD  = 128
};

int mio_obj_hotness_to_pool_idx(uint64_t hotness)
{
	uint64_t hot_thld;
	uint64_t cold_thld;
	int hot_pool_idx = 0;
	int cold_pool_idx = 0;
	int warm_pool_idx;
	int warm_interv;
	int nr_pools = mio_pools.mps_nr_pools;
	struct mio_pool_id *pool_id;

	if (mio_sys_hint_get(MIO_HINT_HOT_OBJ_THRESHOLD, &hot_thld) < 0)
		hot_thld = MIO_DEFAULT_HOT_OBJ_THLD;
	if (mio_sys_hint_get(MIO_HINT_COLD_OBJ_THRESHOLD, &cold_thld) < 0)
		cold_thld = MIO_DEFAULT_COLD_OBJ_THLD;

	/*
	 * Calculate warm pool index according the hotness distance to
	 * hot and cold thresholds.
	 */
	assert(nr_pools >= 1);
	cold_pool_idx = nr_pools - 1;
	assert(hot_pool_idx <= cold_pool_idx);

	if (hotness > hot_thld) {
		pool_id = &mio_pools.mps_pools[hot_pool_idx].mp_id;
		mio_log(MIO_DEBUG,
			"[obj_pool_select] HOT Pool: (%"PRIx64":%"PRIx64")\n",
			pool_id->mpi_hi, pool_id->mpi_lo);
		return hot_pool_idx;
	} else if (hotness < cold_thld) {
		pool_id = &mio_pools.mps_pools[cold_pool_idx].mp_id;
		mio_log(MIO_DEBUG,
			"[obj_pool_select] COLD Pool: (%"PRIx64":%"PRIx64")\n",
			pool_id->mpi_hi, pool_id->mpi_lo);
		return cold_pool_idx;
	} else {
                if (nr_pools <= 2)
                        warm_pool_idx = cold_pool_idx;
                else {  
                        warm_interv = (hot_thld - cold_thld) / (nr_pools - 2);
                        warm_pool_idx = cold_pool_idx -
                                        (hotness - cold_thld) / warm_interv - 1;
                }
                assert(warm_pool_idx >= hot_pool_idx &&
                       warm_pool_idx <= cold_pool_idx);
		pool_id = &mio_pools.mps_pools[warm_pool_idx].mp_id;
		mio_log(MIO_DEBUG,
			"[obj_pool_select] WARM Pool: (%"PRIx64":%"PRIx64")\n",
			pool_id->mpi_hi, pool_id->mpi_lo);
		return warm_pool_idx;
	}
}

struct mio_pool_id mio_obj_hotness_to_pool_id(uint64_t hotness)
{
	int pool_idx;

	pool_idx = mio_obj_hotness_to_pool_idx(hotness);
	return mio_pools.mps_pools[pool_idx].mp_id;
}

/**
 * Set and get system level hints. Currently system hints are session based,
 * support for persistent system hint will added later.
 */
int mio_sys_hint_set(int hint_key, uint64_t hint_value)
{
	int rc;

	rc = mio_hint_add(&mio_sys_hints, hint_key, hint_value);
	if (rc < 0) {
		mio_log(MIO_ERROR,
			"Set system hint failed! error = %d\n", rc);
		return rc;
	}

	return 0;
}

int mio_sys_hint_get(int hint_key, uint64_t *hint_value)
{
	return mio_hint_lookup(&mio_sys_hints, hint_key, hint_value);
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
