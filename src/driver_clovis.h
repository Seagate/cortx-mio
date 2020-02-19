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

#ifndef __DRIVER_CLOVIS_H__
#define __DRIVER_CLOVIS_H__

#include "lib/memory.h"
#include "clovis/clovis.h"
#include "clovis/clovis_idx.h"

#ifndef __MIO_CLOVIS_COMP_OBJ_LAYER_GET_SUPP__
#include "lib/list.h"
#include "lib/tlist.h"
#include "clovis/clovis_layout.h"

#ifndef container_of
#define container_of(ptr, type, member) \
        ((type *)((char *)(ptr)-(char *)(&((type *)0)->member)))
#endif
#endif /* __MIO_CLOVIS_COMP_OBJ_LAYER_GET_SUPP__*/

#define ARRAY_SIZE(a) ((sizeof (a)) / (sizeof (a)[0]))

enum {
	MIO_CLOVIS_MAX_RW_NR_UNITS_PER_OP = 1024 
};

extern struct m0_clovis *mio_clovis_instance;
extern struct m0_clovis_container mio_clovis_container;
extern struct mio_mero_config *mio_clovis_inst_confs;

extern struct mio_comp_obj_ops mio_clovis_comp_obj_ops;

void mio_clovis_driver_register();

/* Helper functions. */
struct m0_bufvec* mio__clovis_bufvec_alloc(int nr);
void mio__obj_id_to_uint128(const struct mio_obj_id *oid,
			    struct m0_uint128 *uint128);
void mio__uint128_to_obj_id(struct m0_uint128 *uint128,
			    struct mio_obj_id *oid);
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
