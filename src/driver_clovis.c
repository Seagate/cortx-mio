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
#include <unistd.h>
#include <sys/types.h>
#include <pwd.h>
#include <grp.h>
#include <uuid/uuid.h>

#include "logger.h"
#include "mio.h"
#include "mio_internal.h"
#include "driver_clovis.h"

struct m0_clovis *mio_clovis_instance;
struct m0_clovis_container mio_clovis_container;
struct m0_clovis_config mio_clovis_conf;
struct mio_mero_config *mio_clovis_inst_confs;

struct m0_uint128 mio_clovis_obj_md_kvs_id;
struct m0_fid mio_clovis_obj_md_kvs_fid = M0_FID_TINIT('x', 0, 0x10);

/**
 * Some helper functions.
 */
void mio__clovis_bufvec_free(struct m0_bufvec *bv)
{
        if (bv == NULL)
                return;

        m0_free(bv->ov_buf);
        m0_free(bv->ov_vec.v_count);
        m0_free(bv);
}

struct m0_bufvec* mio__clovis_bufvec_alloc(int nr)
{
        struct m0_bufvec *bv;

        bv = m0_alloc(sizeof *bv);
        if (bv == NULL)
                return NULL;

        bv->ov_vec.v_nr = nr;
        M0_ALLOC_ARR(bv->ov_vec.v_count, nr);
        if (bv->ov_vec.v_count == NULL)
                goto error;

        M0_ALLOC_ARR(bv->ov_buf, nr);
        if (bv->ov_buf == NULL)
                goto error;

        return bv;

error:
        m0_bufvec_free(bv);
        return NULL;
}

static int clovis_create_obj_attrs_kvs()
{
        int rc;
	struct m0_clovis_op *cops[1] = {NULL};
	struct m0_clovis_idx *idx;

	idx = mio_mem_alloc(sizeof *idx);
	if (idx == NULL)
		return -ENOMEM;

	mio_clovis_obj_md_kvs_id.u_hi = mio_clovis_obj_md_kvs_fid.f_container;
	mio_clovis_obj_md_kvs_id.u_lo = mio_clovis_obj_md_kvs_fid.f_key;
	m0_clovis_idx_init(idx, &mio_clovis_container.co_realm,
			   &mio_clovis_obj_md_kvs_id);

	/* Check if object's attrs key-value store exists. */
        m0_clovis_idx_op(idx, M0_CLOVIS_IC_LOOKUP,
			 NULL, NULL, NULL, 0, &cops[0]);
        m0_clovis_op_launch(cops, 1);
	rc = m0_clovis_op_wait(cops[0],
			       M0_BITS(M0_CLOVIS_OS_FAILED,
				       M0_CLOVIS_OS_STABLE),
			       M0_TIME_NEVER);
	rc = rc? : m0_clovis_rc(cops[0]);
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);

	/*
 	 * Check returned value (rc):
 	 *   - 0: LOOKUP successes and the index has already existed.
 	 *   - -ENOENT: the index doesn't exist yet.
 	 *   - other error code: LOOKUP op failed for some reason.
 	 */
	if (rc != -ENOENT)
		goto exit;

	/* Create the attrs kvs. */
	cops[0] = NULL;
        rc = m0_clovis_entity_create(NULL, &idx->in_entity, &cops[0]);
	if (rc < 0)
		goto exit;
        m0_clovis_op_launch(cops, 1);
	rc = m0_clovis_op_wait(cops[0],
			       M0_BITS(M0_CLOVIS_OS_FAILED,
				       M0_CLOVIS_OS_STABLE),
			       M0_TIME_NEVER);
	rc = rc? : m0_clovis_rc(cops[0]);
	m0_clovis_op_fini(cops[0]);
	m0_clovis_op_free(cops[0]);

exit:
	if (rc == 0)
		mio_obj_attrs_kvs.mk_drv_kvs = idx;
	else {
		m0_clovis_idx_fini(idx);
		mio_mem_free(idx);
	}
	return rc;
}

/**
 * Initialise and finalise clovis instance.
 */
int mio_clovis_init(struct mio *mio_inst)
{
	int rc;
	struct mio_mero_config *drv;
	struct m0_idx_dix_config dix_conf;

	drv = (struct mio_mero_config *)mio_inst->m_driver_confs;
	mio_clovis_inst_confs = drv;

	/* Set clovis configuration parameters. */
	mio_clovis_conf.cc_is_oostore            = drv->mc_is_oostore;
	mio_clovis_conf.cc_is_read_verify        = drv->mc_is_read_verify;
	mio_clovis_conf.cc_local_addr            = drv->mc_clovis_local_addr;
	mio_clovis_conf.cc_ha_addr               = drv->mc_ha_addr;
	mio_clovis_conf.cc_profile               = drv->mc_profile;
	mio_clovis_conf.cc_process_fid           = drv->mc_process_fid;
	mio_clovis_conf.cc_tm_recv_queue_min_len = drv->mc_tm_recv_queue_min_len;
	mio_clovis_conf.cc_max_rpc_msg_size      = drv->mc_max_rpc_msg_size;

	mio_clovis_conf.cc_layout_id =
		m0_clovis_obj_unit_size_to_layout_id(drv->mc_unit_size);

	mio_clovis_conf.cc_idx_service_id   = M0_CLOVIS_IDX_DIX;
	dix_conf.kc_create_meta = false;
	mio_clovis_conf.cc_idx_service_conf = &dix_conf;

	/* Initial clovis instance. */
	rc = m0_clovis_init(&mio_clovis_instance, &mio_clovis_conf, true);
	if (rc != 0)
		return rc;

	/* Initial a container. */
	m0_clovis_container_init(&mio_clovis_container, NULL,
				 &M0_CLOVIS_UBER_REALM,
				 mio_clovis_instance);
	rc = mio_clovis_container.co_realm.re_entity.en_sm.sm_rc;
	if (rc != 0) {
		mio_log(MIO_ERROR, "Failed to open Clovis's uber realm!\n");
		goto error;
	}

	/* Create object attrs kvs if it doesn't exist. */
 	rc = clovis_create_obj_attrs_kvs();
	if (rc != 0) {
		mio_log(MIO_ERROR, "Failed to create attrs key-value set!\n");
		goto error;
	}
	return 0;

error:
	m0_clovis_fini(mio_clovis_instance, true);
	return rc;
}

static void mio_clovis_fini()
{
	m0_clovis_idx_fini(
		(struct m0_clovis_idx *)mio_obj_attrs_kvs.mk_drv_kvs);
	m0_clovis_fini(mio_clovis_instance, true);
	mio_clovis_instance = NULL;
}

static int mio_clovis_thread_init(struct mio_thread *thread)
{
	struct m0_thread *mthread;

	mthread = mio_mem_alloc(sizeof(*mthread));
	if (mthread == NULL)
		return -ENOMEM;
	memset(mthread, 0, sizeof(struct m0_thread));
	m0_thread_adopt(mthread, mio_clovis_instance->m0c_mero);
	thread->mt_drv_thread = mthread;
	return 0;
}

static void mio_clovis_thread_fini(struct mio_thread *thread)
{
	m0_thread_shun();
	mio_mem_free(thread->mt_drv_thread);
}

static int mio_clovis_user_perm(struct mio *mio_inst)
{
	int i;
	int rc = 0;
	int nr_grps = 0;
	gid_t *gids;
	struct group *mero_grp;
	struct passwd *mio_user_pw;
	struct mio_mero_config *drv;

	errno = 0;
	mio_user_pw = getpwuid(getuid());
	if (mio_user_pw == NULL) {
		fprintf(stderr, "Failed to get user ID: errno = -%d, %s\n",
			errno, strerror(errno));
		return -errno;
	}

	/* If it is `root`. */
	if (mio_user_pw->pw_uid == 0)
		return 0;

	/* Check if the user is in the Mero group. */
	drv = (struct mio_mero_config *)mio_inst->m_driver_confs;
	if (drv->mc_mero_group == NULL) {
		fprintf(stderr,
			"No Mero group is specified and user is not root!\n");
		return -EPERM;
	}
	mero_grp = getgrnam(drv->mc_mero_group);
	if (mero_grp == NULL) {
		fprintf(stderr, "Mero group [%s] doesn't exist!\n",
			drv->mc_mero_group);
		return -EPERM;
	}

	getgrouplist(mio_user_pw->pw_name, mio_user_pw->pw_gid,
		     NULL, &nr_grps);
	if (nr_grps == 0)
		return -EPERM;
	gids = mio_mem_alloc(nr_grps * sizeof(int));
	if (gids == NULL) {
		rc = -ENOMEM;
		goto exit;
	}

	rc = getgrouplist(mio_user_pw->pw_name, mio_user_pw->pw_gid,
			  gids, &nr_grps);
	if (rc < 0)
		goto exit;
	for (i = 0; i < nr_grps; i++) {
		if (gids[i] == mero_grp->gr_gid)
			break;
	}
	if (i == nr_grps)
		rc = -EPERM;
	else
		rc = 0;

exit:
	mio_mem_free(gids);
	return rc;
}

static struct mio_driver_sys_ops mio_clovis_sys_ops = {
        .mdo_init = mio_clovis_init,
        .mdo_fini = mio_clovis_fini,
        .mdo_user_perm = mio_clovis_user_perm,
        .mdo_thread_init = mio_clovis_thread_init,
        .mdo_thread_fini = mio_clovis_thread_fini
};

static void mio_clovis_op_fini(struct mio_op *mop)
{
	struct m0_clovis_op *cop;
	struct mio_driver_op *dop;

	dop = mop->mop_drv_op_chain.mdoc_head;
	while(dop != NULL) {
		mop->mop_drv_op_chain.mdoc_head = dop->mdo_next;
		dop->mdo_next = NULL;

		cop = (struct m0_clovis_op *)dop->mdo_op;
		m0_clovis_op_fini(cop);
		m0_clovis_op_free(cop);
		if (dop->mdo_op_fini)
			dop->mdo_op_fini(dop);
		mio_mem_free(dop);

		dop = mop->mop_drv_op_chain.mdoc_head;
	}
}

static int
mio_clovis_op_wait(struct mio_op *mop, uint64_t timeout, int *retstate)
{
	int rc;
	struct m0_clovis_op *cop;

	cop = MIO_CLOVIS_OP(mop);
	rc = m0_clovis_op_wait(cop, M0_BITS(M0_CLOVIS_OS_STABLE,
					    M0_CLOVIS_OS_FAILED),
			       timeout);
	/*
	 * Check returned value (rc) from m0_clovis_op_wait:
	 *   - errors (rc < 0), treat timeout error differently.
	 *   - rc == 0, the operation is completed, then check the
	 *     operation's rc value to see if the op is successful or failed.
 	 */
	if (rc == 0) {
		if (m0_clovis_rc(cop) < 0) {
			*retstate = MIO_OP_FAILED;
			rc = m0_clovis_rc(cop);
		} else
			*retstate = MIO_OP_COMPLETED;
	} else if (rc == -ETIMEDOUT)
		*retstate = MIO_OP_ONFLY;
	else
		*retstate = MIO_OP_FAILED;
	return rc;
}

/**
 * The callback functions defined for MIO operation have different
 * arguments with the ones of drivers', such as Clovis. A jumper
 * function here is used to call the callback functions set by MIO
 * applications.
 *
 * The callback functions shown below give examples on how to relay
 * control to callbacks set by apps.
 *
 * This looks a bit ugly, is there any better solution?
 */
static void clovis_op_cb_complete(struct m0_clovis_op *cop)
{
	int rc;
	struct mio_op *mop;

	mop = (struct mio_op *)cop->op_datum;
	if (!mop->mop_drv_op_chain.mdoc_head->mdo_post_proc)
		goto app_cb;

	rc = mop->mop_drv_op_chain.mdoc_head->mdo_post_proc(mop);
	if (rc == MIO_DRV_OP_NEXT)
		return;

app_cb:
	mio_driver_op_invoke_real_cb(mop, 0);
}

static void clovis_op_cb_failed(struct m0_clovis_op *cop)
{
	struct mio_op *mop;

	mop = (struct mio_op *)cop->op_datum;
	mio_driver_op_invoke_real_cb(mop, m0_clovis_rc(cop));
}

static struct m0_clovis_op_ops clovis_op_cbs;
static int
mio_clovis_op_set_cbs(struct mio_op *mop)

{
	struct m0_clovis_op *cop;

	assert(mop != NULL);

	clovis_op_cbs.oop_executed = NULL;
	clovis_op_cbs.oop_stable = clovis_op_cb_complete;
	clovis_op_cbs.oop_failed = clovis_op_cb_failed;
	cop = MIO_CLOVIS_OP(mop);
	cop->op_datum = (void *)mop;
	m0_clovis_op_setup(cop, &clovis_op_cbs, 0);

	return 0;
}

static struct mio_op_ops mio_clovis_op_ops = {
	.mopo_fini    = mio_clovis_op_fini,
	.mopo_wait    = mio_clovis_op_wait,
	.mopo_set_cbs = mio_clovis_op_set_cbs
};

void mio_clovis_driver_register()
{
	mio_driver_register(
		MIO_MERO, &mio_clovis_sys_ops,
		&mio_clovis_op_ops, &mio_clovis_obj_ops,
		&mio_clovis_kvs_ops, &mio_clovis_comp_obj_ops);
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
