/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */


#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <asm/byteorder.h>

#include "kvs.h"
#include "helpers.h"

struct kvs_id_uint128 {
	uint64_t k_hi;
	uint64_t k_lo;
};

enum {
	KVS_MAX_KEY_LEN = 64,
	KVS_MAX_VAL_LEN = 64,
	KVS_MAX_NR_PAIRS_PER_OP = 1024
};

#define KVS_VAL_STRING  ("MIO KVS demo")

static int kvs_id_sscanf(char *idstr, struct mio_kvs_id *kid)
{
        int rc;
        int n;
        uint64_t u1;
        uint64_t u2;

        rc = sscanf(idstr, "%"SCNx64" : %"SCNx64" %n", &u1, &u2, &n);
        if (rc < 0)
                return rc;
        u1 = __cpu_to_be64(u1);
        u2 = __cpu_to_be64(u2);

        memcpy(kid->mki_bytes, &u1, sizeof u1);
        memcpy(kid->mki_bytes + sizeof u1, &u2, sizeof u2);
        return 0;
}

static void kvs_id_to_uint128(struct kvs_id_uint128 *kid128,
			      struct mio_kvs_id *kid)
{
	uint8_t *ptr;
        uint64_t u1;
        uint64_t u2;

	ptr = kid->mki_bytes;
	memcpy(&u1, ptr, sizeof u1);
	ptr += sizeof u1;
	memcpy(&u2, ptr, sizeof u2);

	u1 = __be64_to_cpu(u1);
	u2 = __be64_to_cpu(u2);
	kid128->k_hi = u1;
	kid128->k_lo = u2;
}

static void kvs_free_pairs(struct mio_kv_pair *pairs)
{
	if (pairs != NULL)
		free(pairs);
}

static struct mio_kv_pair* kvs_alloc_pairs(int nr)
{
	struct mio_kv_pair *pairs;

	pairs = malloc(nr * sizeof(struct mio_kv_pair));
	return pairs;
}

static
int kvs_fill_pairs(struct mio_kvs_id *kid, struct mio_kv_pair *pairs,
		   int start_kno, int nr_pairs, bool set_vals)
{
	int   i;
	int   rc;
	int   klen = 0;
	int   vlen = 0;
	char *tmp_str = NULL;
	char *key_str = NULL;
	char *val_str = NULL;
	struct kvs_id_uint128 id128;

	if (pairs == NULL)
		return -EINVAL;

	rc = -ENOMEM;
	tmp_str = malloc(KVS_MAX_KEY_LEN);
	if (tmp_str == NULL)
		goto error;

	/*
	 * Keys are flled with this format (index fid:key's serial number).
	 */
	kvs_id_to_uint128(&id128, kid);
	for (i = 0; i < nr_pairs; i++) {
		sprintf(tmp_str,
			"%"PRIx64":%"PRIx64":%d",
			id128.k_hi, id128.k_lo, start_kno + i);
		klen = strnlen(tmp_str, KVS_MAX_KEY_LEN) + 1;
		key_str = malloc(klen);
		if (key_str == NULL)
			goto error;
		memcpy(key_str, tmp_str, klen);
		pairs[i].mkp_klen = klen;
		pairs[i].mkp_key  = key_str;

		if (set_vals) {
			memset(tmp_str, 0, KVS_MAX_VAL_LEN);
			sprintf(tmp_str, "%s %d",
				KVS_VAL_STRING, (int)mio_cmd_random(nr_pairs));
			vlen = strnlen(tmp_str, KVS_MAX_VAL_LEN) + 1;
			val_str = malloc(vlen);
			if (val_str == NULL)
				goto error;
			memcpy(val_str, tmp_str, vlen);
			pairs[i].mkp_vlen = vlen;
			pairs[i].mkp_val  = val_str;
		}
	}

	return 0;

error:
	if (tmp_str)
		free(tmp_str);
	if (key_str)
		free(key_str);
	if (val_str)
		free(val_str);

	for (i = 0; i < nr_pairs; ++i) {
		if (pairs[i].mkp_key)
			free(pairs[i].mkp_key);
		if (pairs[i].mkp_val)
			free(pairs[i].mkp_val);
		pairs[i].mkp_klen = 0;
		pairs[i].mkp_vlen = 0;
	}

	return rc;
}

static void
kvs_print_pairs(struct mio_kv_pair *pairs, int nr_pairs, FILE *log)
{
	int i;

	for (i = 0; i < nr_pairs; i++) {
		fprintf(log, "%s\t\t%s\n",
			(char *)pairs[i].mkp_key, (char *)pairs[i].mkp_val);
	}
}

static int kvs_create_set(struct mio_kvs_id *kid)
{
	int rc;
	struct mio_op op;

	mio_op_init(&op);
	rc = mio_kvs_create_set(kid, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in creating an KVS set!\n");

	mio_op_fini(&op);
	return rc;
}

static int kvs_delete_set(struct mio_kvs_id *kid)
{
	int rc;
	struct mio_op op;

	mio_op_init(&op);
	rc = mio_kvs_del_set(kid, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in creating an KVS set!\n");

	mio_op_fini(&op);
	return rc;
}

static int
kvs_query_put(struct mio_kvs_id *kid, int start_kno, int nr_kvp, FILE *log)
{
	int rc;
	int *rcs;
	struct mio_op op;
	struct mio_kv_pair *pairs;

	rcs = malloc(nr_kvp * sizeof(int));
	if (rcs == NULL)
		return -ENOMEM;

	pairs = kvs_alloc_pairs(nr_kvp);
	if (pairs == NULL) {
		free(rcs);
		return -ENOMEM;
	}
	kvs_fill_pairs(kid, pairs, start_kno, nr_kvp, true);
	if (log)
		kvs_print_pairs(pairs, nr_kvp, log);

	mio_op_init(&op);
	rc = mio_kvs_pair_put(kid, nr_kvp, pairs, rcs, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in inserting kv pairs to aset!\n");
	mio_op_init(&op);

	kvs_free_pairs(pairs);
	return rc;
}

static int
kvs_query_get(struct mio_kvs_id *kid, int start_kno, int nr_kvp, FILE *log)
{
	int rc;
	int *rcs;
	struct mio_op op;
	struct mio_kv_pair *pairs;

	rcs = malloc(nr_kvp * sizeof(int));
	if (rcs == NULL)
		return -ENOMEM;
	pairs = kvs_alloc_pairs(nr_kvp);
	if (pairs == NULL) {
		free(rcs);
		return -ENOMEM;
	}
	kvs_fill_pairs(kid, pairs, start_kno, nr_kvp, false);

	mio_op_init(&op);
	rc = mio_kvs_pair_get(kid, nr_kvp, pairs, rcs, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in retrieving kv pairs to aset!\n");
	mio_op_fini(&op);

	if (rc == 0 && log != NULL)
		kvs_print_pairs(pairs, nr_kvp, log);

	kvs_free_pairs(pairs);
	return rc;
}

static int
kvs_query_next(struct mio_kvs_id *kid, int *last_kno,
	       bool exclude_start_kno, int nr_kvp, FILE *log)
{
	int i;
	int rc;
	int *rcs;
	uint64_t u1;
	uint64_t u2;
	struct mio_op op;
	struct mio_kv_pair *pairs;

	rcs = malloc(nr_kvp * sizeof(int));
	if (rcs == NULL)
		return -ENOMEM;
	pairs = kvs_alloc_pairs(nr_kvp);
	if (pairs == NULL) {
		free(rcs);
		return -ENOMEM;
	}
	kvs_fill_pairs(kid, pairs, *last_kno, 1, false);

	mio_op_init(&op);
	rc = mio_kvs_pair_next(
		kid, nr_kvp, pairs, exclude_start_kno, rcs, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in retrieving kv pairs to aset!\n");
	mio_op_fini(&op);

	if (rc == 0 && log != NULL)
		kvs_print_pairs(pairs, nr_kvp, log);

	for (i = 0; i < nr_kvp; i++)
		if (rcs[i] < 0)
			break;
	if (i > 0) {
		rc = sscanf(pairs[i-1].mkp_key,
			    "%"SCNx64":%"SCNx64":%d", &u1, &u2, last_kno);
		if (rc < 0)
			goto exit;
		if (i == nr_kvp)
			rc = 0;
		else
			rc = rcs[i] == EOF? EOF : rcs[i];
	} else
		rc = rcs[0];

exit:
	kvs_free_pairs(pairs);
	return rc;
}

static int
kvs_query_del(struct mio_kvs_id *kid, int start_kno, int nr_kvp, FILE *log)
{
	int rc;
	int *rcs;
	struct mio_op op;
	struct mio_kv_pair *pairs;

	rcs = malloc(nr_kvp * sizeof(int));
	if (rcs == NULL)
		return -ENOMEM;
	pairs = kvs_alloc_pairs(nr_kvp);
	if (pairs == NULL) {
		free(rcs);
		return -ENOMEM;
	}
	kvs_fill_pairs(kid, pairs, start_kno, nr_kvp, false);
	if (log != NULL)
		kvs_print_pairs(pairs, nr_kvp,log);

	mio_op_init(&op);
	rc = mio_kvs_pair_del(kid, nr_kvp, pairs, rcs, &op);
	if (rc != 0)
		return rc;

	rc = mio_cmd_wait_on_op(&op);
	if (rc < 0)
		fprintf(stderr, "Failed in retrieving kv pairs to aset!\n");
	mio_op_fini(&op);

	kvs_free_pairs(pairs);
	return rc;
}

int mio_cmd_kvs_create_set(struct mio_kvs_id *kid)
{
	return kvs_create_set(kid);
}

int mio_cmd_kvs_delete_set(struct mio_kvs_id *kid)
{
	return kvs_delete_set(kid);
}

int mio_cmd_kvs_insert_pairs(struct mio_kvs_id *kid,
			     int start_kno, int nr_pairs, FILE *log)
{
	int rc = 0;

	while(nr_pairs > 0) {
		int nr_pairs_op = nr_pairs > KVS_MAX_NR_PAIRS_PER_OP?
				  KVS_MAX_NR_PAIRS_PER_OP : nr_pairs;
		rc = kvs_query_put(kid, start_kno, nr_pairs_op, log);
		if (rc != 0)
			break;
		nr_pairs -= nr_pairs_op;
		start_kno += nr_pairs_op;
	}

	return rc;
}

int mio_cmd_kvs_retrieve_pairs(struct mio_kvs_id *kid,
			       int start_kno, int nr_pairs, FILE *log)
{
	int rc = 0;

	while(nr_pairs > 0) {
		int nr_pairs_op = nr_pairs > KVS_MAX_NR_PAIRS_PER_OP?
				  KVS_MAX_NR_PAIRS_PER_OP : nr_pairs;

		rc = kvs_query_get(kid, start_kno, nr_pairs_op, log);
		if (rc != 0)
			break;
		nr_pairs -= nr_pairs_op;
		start_kno += nr_pairs_op;
	}

	return rc;
}

int mio_cmd_kvs_list_pairs(struct mio_kvs_id *kid,
			   int start_kno, int nr_pairs, FILE *log)
{
	int rc = 0;
	int last_kno;
	bool first_round = true;

	last_kno = start_kno;
	while(nr_pairs > 0) {
		int nr_pairs_op = nr_pairs > KVS_MAX_NR_PAIRS_PER_OP?
				  KVS_MAX_NR_PAIRS_PER_OP : nr_pairs;

		rc = kvs_query_next(
			kid, &last_kno, !first_round, nr_pairs_op, log);
		first_round = false;
		if (rc < 0)
			break;

		nr_pairs -= nr_pairs_op;
	}

	if (rc == EOF)
		rc = 0;
	return rc;
}

int mio_cmd_kvs_del_pairs(struct mio_kvs_id *kid,
			  int start_kno, int nr_pairs, FILE *log)
{
	int rc = 0;
	int nr_pairs_op;

	while(nr_pairs > 0) {
		nr_pairs_op = nr_pairs > KVS_MAX_NR_PAIRS_PER_OP?
			      KVS_MAX_NR_PAIRS_PER_OP : nr_pairs;

		rc = kvs_query_del(kid, start_kno, nr_pairs_op, log);
		if (rc != 0)
			break;
		nr_pairs -= nr_pairs_op;
		start_kno += nr_pairs_op;
	}

	return rc;
}

int mio_cmd_kvs_args_init(int argc, char **argv,
			  struct mio_cmd_kvs_params *params,
			  void (*usage)(FILE *, char *))
{
	int v;
	int option_index = 0;
	static struct option l_opts[] = {
				{"kvs",       required_argument, NULL, 'k'},
				{"start_key", required_argument, NULL, 's'},
				{"nr_pairs",  required_argument, NULL, 'n'},
				{"mio_conf",  required_argument, NULL, 'y'},
				{"log_file",  required_argument, NULL, 'l'},
				{"help",      no_argument,       NULL, 'h'},
				{0,           0,                 0,     0 }};

	memset(params, 0, sizeof *params);
	params->ckp_nr_pairs = 1;

	while ((v = getopt_long(argc, argv, ":k:s:n:y:l:h", l_opts,
				&option_index)) != -1)
	{
		switch (v) {
		case 'k':
			kvs_id_sscanf(optarg, &params->ckp_kid);
			continue;
		case 's':
			params->ckp_start_kno = atoi(optarg);
			continue;
		case 'n':
			params->ckp_nr_pairs = atoi(optarg);
			continue;
		case 'y':
			params->ckp_conf_fname = strdup(optarg);
			if (params->ckp_conf_fname == NULL)
				exit(EXIT_FAILURE);
			continue;
		case 'l':
			params->ckp_log = strdup(optarg);
			if (params->ckp_log == NULL)
				exit(EXIT_FAILURE);
			continue;
		case 'h':
			usage(stderr, basename(argv[0]));
			exit(EXIT_FAILURE);
		case '?':
			fprintf(stderr, "Unsupported option '%c'\n",
				optopt);
			usage(stderr, basename(argv[0]));
			exit(EXIT_FAILURE);
		case ':':
			fprintf(stderr, "No argument given for '%c'\n",
				optopt);
			usage(stderr, basename(argv[0]));
			exit(EXIT_FAILURE);
		default:
			fprintf(stderr, "Unsupported option '%c'\n", v);
		}
	}

	return 0;
}

void  mio_cmd_kvs_args_fini(struct mio_cmd_kvs_params *params)
{
	if(params->ckp_conf_fname)
		free(params->ckp_conf_fname);
	if (params->ckp_log)
		free(params->ckp_log);
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
