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
#include "mio.h"
#include "mio_internal.h"

enum {
	MIO_HINT_INVALID = -1
};

struct hint {
	char *h_name;
	enum mio_hint_type h_type;
};

static struct hint hint_table[] = {
	[MIO_HINT_OBJ_CACHE_FLUSH_SIZE] = {
		.h_name = "MIO_HINT_OBJ_CACHE_FLUSH_SIZE",
		.h_type = MIO_HINT_SESSION,
	},
	[MIO_HINT_OBJ_LIFETIME] = {
		.h_name = "MIO_HINT_OBJ_LIFETIME",
		.h_type = MIO_HINT_PERSISTENT,
	},
	[MIO_HINT_OBJ_WHERE] = {
		.h_name = "MIO_HINT_OBJ_WHERE",
		.h_type = MIO_HINT_SESSION,
	}
};

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

#define NKEYS (sizeof(hint_table)/sizeof(struct hint))
enum mio_hint_type mio_hint_type(enum mio_hint_key key)
{
	if (key < 0 || key >= NKEYS)
		return -EINVAL;
	return hint_table[key].h_type;
}

char* mio_hint_name(enum mio_hint_key key)
{
	if (key < 0 || key >= NKEYS)
		return NULL;
	return hint_table[key].h_name;
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
static int mio_obj_hint_store(struct mio_obj *obj)
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
		if (mio_hint_type(obj->mo_hints.mh_map.mhm_keys[i]) ==
		    MIO_HINT_PERSISTENT)
			nr_phints++;

	if (phints->mh_map.mhm_nr_entries != 0)
		mio_hint_map_fini(&phints->mh_map);
	rc = mio_hint_map_init(&phints->mh_map, MIO_OBJ_HINT_NUM);
	if (rc < 0)
		return rc;

	for (i = 0; i < nr_hints; i++) {
		if (mio_hint_type(obj->mo_hints.mh_map.mhm_keys[i]) ==
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

static int mio_obj_hint_load(struct mio_obj *obj)
{
	if (obj == NULL)
		return -EINVAL;
	return drv_obj_ops->moo_hint_load(obj);
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

int mio_obj_hints_set(struct mio_obj *obj, struct mio_hints *hints)
{
	int rc;

	rc = mio_obj_hint_ops_check();
	if (rc < 0)
		return rc;
	if (obj == NULL || hints == NULL)
		return -EINVAL;
	
	rc = mio_hint_map_copy(&obj->mo_hints.mh_map, &hints->mh_map)? :
	     mio_obj_hint_store(obj);
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

	rc = mio_obj_hint_load(obj);
	if (rc < 0)
		return rc;
	
	rc = mio_hint_map_copy(&hints->mh_map, &obj->mo_hints.mh_map);
	return rc;
}

/**
 * Helper functions to set or get individual hint.
 */
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
