/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <assert.h>
#include <sys/errno.h>

#include "mio_internal.h"
#include "mio.h"
#include "driver_motr.h"

static struct mio_driver mio_drivers[MIO_DRIVER_NUM];

/**
 * This function must be called in each driver specific operation
 * functions for object, key/value and others before launching
 * the operations to add driver specific operation into the chain.
 */
int mio_driver_op_add(struct mio_op *op,
		      mio_driver_op_postprocess post_proc,
		      void *post_proc_data,
		      mio_driver_op_fini op_fini,
		      void *drv_op, void *drv_op_args)
{
	struct mio_driver_op *dop;

	assert(op != NULL);

	dop = (struct mio_driver_op *)mio_mem_alloc(sizeof *dop);
	if (dop == NULL)
		return -ENOMEM;

	/*
 	 * Set driver op's action function such as post-processing
 	 * for key/value set GET query. See struct mio_driver_op
 	 * for details.
 	 */
	dop->mdo_op = drv_op;
	dop->mdo_op_args = drv_op_args;
	dop->mdo_post_proc = post_proc;
	dop->mdo_post_proc_data = post_proc_data;
	dop->mdo_op_fini = op_fini;

	/* Insert into the chain. */
	dop->mdo_next = op->mop_drv_op_chain.mdoc_head;
	op->mop_drv_op_chain.mdoc_head = dop;

	/*
	 * Set driver operation's callbacks which will invoke real
	 * application set callbacks when all job of MIO op is done.
	 */
	if (op->mop_op_ops != NULL && op->mop_op_ops->mopo_set_cbs &&
	    op->mop_app_cbs.moc_cb_complete != NULL &&
	    op->mop_app_cbs.moc_cb_failed != NULL) {
		op->mop_op_ops->mopo_set_cbs(op);
	}

	return 0;
}

void mio_driver_op_invoke_real_cb(struct mio_op *op, int rc)
{
	struct mio_op_app_cbs *app_cbs;

	assert(op != NULL);

	app_cbs = &op->mop_app_cbs;
	op->mop_rc = rc;
	if (rc == 0)
		app_cbs->moc_cb_complete(op);
	else
		app_cbs->moc_cb_failed(op);
}

struct mio_driver* mio_driver_get(enum mio_driver_id driver_id)
{
	return mio_drivers + driver_id;
}

void mio_driver_register(enum mio_driver_id driver_id,
			 struct mio_driver_sys_ops *sys_ops,
			 struct mio_pool_ops *pool_ops,
			 struct mio_op_ops *op_ops,
			 struct mio_obj_ops *obj_ops,
			 struct mio_kvs_ops *kvs_ops,
			 struct mio_comp_obj_ops *comp_obj_ops)
{
	struct mio_driver *drv;

	drv = mio_drivers + driver_id;
	drv->md_sys_ops = sys_ops;
	drv->md_pool_ops = pool_ops;
	drv->md_op_ops = op_ops;
	drv->md_obj_ops = obj_ops;
	drv->md_kvs_ops = kvs_ops;
	drv->md_comp_obj_ops = comp_obj_ops;
}

void mio_drivers_register()
{
	mio_motr_driver_register();
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
