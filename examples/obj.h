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

#ifndef __OBJ_H__
#define __OBJ_H__

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

#include "src/mio.h"

enum {
	MIO_CMD_MAX_BLOCK_COUNT_PER_OP = 120
};

struct mio_cmd_obj_params {
	char *cop_conf_fname;

	struct mio_pool_id cop_pool_id;

	struct mio_obj_id cop_oid;
	int cop_nr_objs;
	uint64_t cop_block_size;
	uint64_t cop_block_count;

	bool cop_async_mode;

	int cop_nr_threads;
};

extern bool print_on_console;

int mio_cmd_obj_args_init(int argc, char **argv,
			  struct mio_cmd_obj_params *params,
			  void (*usage)(FILE *, char *));
void mio_cmd_obj_args_fini(struct mio_cmd_obj_params *params);

int mio_cmd_obj_write(char *src, struct mio_obj_id *oid,
		      uint32_t block_size, uint32_t block_count);

int mio_cmd_obj_write_async(char *src, struct mio_obj_id *oid,
			    uint32_t block_size, uint32_t block_count);

int mio_cmd_obj_read(struct mio_obj_id *oid, char *dest,
		     uint32_t block_size, uint32_t block_count);
int mio_cmd_obj_read_async(struct mio_obj_id *oid, char *dest,
			   uint32_t block_size, uint32_t block_count);

int mio_cmd_obj_touch(struct mio_obj_id *oid);
int mio_cmd_obj_unlink(struct mio_obj_id *id);

int mio_cmd_obj_open(struct mio_obj_id *oid,struct mio_obj *obj);
void mio_cmd_obj_close(struct mio_obj *obj);

/** Helper functions. */
int obj_alloc_iovecs(struct mio_iovec **data, uint32_t bcount,
		     uint32_t bsize, uint64_t offset, uint64_t max_offset);
void obj_cleanup_iovecs(struct mio_iovec *data);
int obj_read_data_from_file(FILE *fp, uint32_t bcount, uint32_t bsize,
			    struct mio_iovec *data);
int obj_write_data_to_file(FILE *fp, uint32_t bcount, struct mio_iovec *data);

int obj_open(struct mio_obj_id *oid, struct mio_obj *obj);
void obj_close(struct mio_obj *obj);
int obj_create(struct mio_obj_id *oid, struct mio_obj *obj);
int obj_rm(struct mio_obj_id *oid);

int obj_write(struct mio_obj *obj, uint32_t bcount, struct mio_iovec *data);
int obj_read(struct mio_obj *obj, uint32_t bcount, struct mio_iovec *data);

void obj_id_printf(struct mio_obj_id *oid);

#endif /* __OBJ_H__ */

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
