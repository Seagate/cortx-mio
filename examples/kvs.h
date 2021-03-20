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

#ifndef __KVS_H__
#define __KVS_H__

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include "src/mio.h"

struct mio_cmd_kvs_params {
	char *ckp_conf_fname;

	struct mio_kvs_id ckp_kid;
	int ckp_nr_pairs;
	int ckp_start_kno;

	char *ckp_log;
};

int mio_cmd_kvs_args_init(int argc, char **argv,
			  struct mio_cmd_kvs_params *params,
			  void (*usage)(FILE *, char *));
void  mio_cmd_kvs_args_fini(struct mio_cmd_kvs_params *params);

int mio_cmd_kvs_create_set(struct mio_kvs_id *kid);
int mio_cmd_kvs_delete_set(struct mio_kvs_id *kid);

int mio_cmd_kvs_insert_pairs(struct mio_kvs_id *kid,
			     int start_kno, int nr_pairs, FILE *log);
int mio_cmd_kvs_retrieve_pairs(struct mio_kvs_id *kid,
			       int start_kno, int nr_pairs, FILE *log);
int mio_cmd_kvs_list_pairs(struct mio_kvs_id *kid,
			   int start_kno, int nr_pairs, FILE *log);
int mio_cmd_kvs_del_pairs(struct mio_kvs_id *kid,
			  int start_kno, int nr_pairs, FILE *log);

#endif /* __KVS_H__ */

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
