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

#ifndef __HELPERS_H__
#define __HELPERS_H__

int mio_cmd_obj_id_clone(struct mio_obj_id *orig_oid,
			 struct mio_obj_id *new_oid,
			 int off1, int off2);
int mio_cmd_id_sscanf(char *idstr, uint64_t *u1, uint64_t *u2);
int mio_cmd_strtou64(const char *arg, uint64_t *out);
int mio_cmd_wait_on_op(struct mio_op *op);
uint32_t mio_cmd_random(uint32_t max);

void mio_cmd_error(char *msg, int error);

int mio_cmd_thread_init(pthread_t **ret_th, void* (*func)(void *), void *args);
int mio_cmd_thread_join(pthread_t *th);
int mio_cmd_thread_fini(pthread_t *th);

#endif /* __HELPERS_H__ */

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
