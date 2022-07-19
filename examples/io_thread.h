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

#ifndef __IO_THREAD_H__
#define __IO_THREAD_H__

enum {
        IO_THREAD_MAX_RAND_NUM = 1024
};

void io_thread_generate_data(uint32_t bcount, uint32_t bsize,
			struct mio_iovec *data, MD5_CTX *md5_ctx);
int io_thread_obj_write(struct mio_obj *obj, uint32_t block_size,
			uint32_t block_count, unsigned char *md5sum);
int io_thread_obj_read(struct mio_obj *obj, uint32_t block_size,
		       uint32_t block_count, unsigned char *md5sum);
void io_threads_stop(pthread_t **threads, int nr_threads);
#endif
