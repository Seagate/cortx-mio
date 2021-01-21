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

#ifndef __DRIVER_MOTR_H__
#define __DRIVER_MOTR_H__

#include "lib/memory.h"
#include "motr/client.h"
#include "motr/idx.h"

#ifndef __MIO_MOTR_COMP_OBJ_LAYER_GET_SUPP__
#include "lib/list.h"
#include "lib/tlist.h"
#include "motr/layout.h"

#ifndef container_of
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif
#endif /* __MIO_MOTR_COMP_OBJ_LAYER_GET_SUPP__*/

#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof (a)[0]))

#define MIO_MOTR_OP(op) \
	((struct m0_op *)op->mop_drv_op_chain.mdoc_head->mdo_op)

enum {
	MIO_MOTR_RW_MAX_UNITS_PER_OP = 128
};

extern struct m0_client *mio_motr_instance;
extern struct m0_container mio_motr_container;
extern struct mio_motr_config *mio_motr_inst_confs;

extern struct m0_uint128 mio_motr_obj_md_kvs_id;
extern struct m0_fid mio_motr_obj_md_kvs_fid;

extern struct mio_obj_ops mio_motr_obj_ops;
extern struct mio_kvs_ops mio_motr_kvs_ops;
extern struct mio_comp_obj_ops mio_motr_comp_obj_ops;

void mio_motr_driver_register();

/* Helper functions. */
struct m0_bufvec* mio__motr_bufvec_alloc(int nr);
void mio__motr_bufvec_free(struct m0_bufvec *bv);
void mio__obj_id_to_uint128(const struct mio_obj_id *oid,
			    struct m0_uint128 *uint128);
void mio__uint128_to_obj_id(struct m0_uint128 *uint128,
			    struct mio_obj_id *oid);
void mio__motr_pool_id_to_fid(const struct mio_pool_id *pool_id,
			      struct m0_fid *fid);
void mio__motr_fid_to_pool_id(const struct m0_fid *fid,
			      struct mio_pool_id *pool_id);
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
