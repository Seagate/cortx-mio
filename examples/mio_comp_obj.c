/* -*- C -*- */
/*
 * Copyright: (c) 2020 - 2021 Seagate Technology LLC and/or its its Affiliates,
 * All Rights Reserved
 *
 * This software is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <stdio.h>
#include <stdlib.h>
#include <asm/byteorder.h>

#include "obj.h"
#include "helpers.h"

static void comp_obj_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]...\n"
"Examples for composite object APIs.\n"
"\n"
"Mandatory arguments to long options are mandatory for short options too.\n"
"  -o, --object         OID       ID of the Motr object\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
, prog_name);
}

/**
 * Calculate the layer's ids from composite object id. Here we
 * naviely assume a simple relation between composite object id
 * and layers'.
 */
static void layer_ids_get(struct mio_obj_id *oid, int nr_layers,
			  struct mio_obj_id *layer_ids)
{
	int i;
	uint64_t u1;
	uint64_t u2;
	uint64_t n1;
	uint64_t n2;

	memcpy(&u1, oid->moi_bytes, sizeof u1);
	memcpy(&u2, oid->moi_bytes + sizeof u1, sizeof u2);
	u1 = __be64_to_cpu(u1);
	u2 = __be64_to_cpu(u2);

	for (i = 0; i < nr_layers; i++) {
		struct mio_obj_id *layer_id = layer_ids + i;
		n1 = u1 + i + 1;
		n2 = u2;
		n1 = __cpu_to_be64(n1);
		n2 = __cpu_to_be64(n2);
		memcpy(layer_id->moi_bytes, &n1, sizeof n1);
		memcpy(layer_id->moi_bytes + sizeof n1, &n2, sizeof n2);
	}
}

static int
comp_obj_alloc_layer_ids_n_exts(struct mio_obj_id *oid,
				struct mio_obj_id **layer_ids, int nr_layers,
				struct mio_obj_ext **exts, int nr_exts)
{
	if (layer_ids == NULL || exts == NULL)
		return -EINVAL;

	*layer_ids = malloc(nr_layers * sizeof(**layer_ids));
	*exts = malloc(nr_exts * sizeof(**exts));
	if (*layer_ids == NULL || *exts == NULL) {
		if (*layer_ids == NULL)
			free(*layer_ids);
		if (*exts == NULL)
			free(*exts);
		return -ENOMEM;
	}

	layer_ids_get(oid, nr_layers, *layer_ids);
	return 0;
}

static int
comp_obj_add_extents(struct mio_obj *obj, int nr_layers, int nr_exts)
{
	int i;
	int j;
	int rc = 0;
	struct mio_obj_id *layer_ids;
	struct mio_obj_ext *exts;
	struct mio_op op;

	rc = comp_obj_alloc_layer_ids_n_exts(
		&obj->mo_id, &layer_ids, nr_layers, &exts, nr_exts);
	if (rc < 0)
		return rc;

	for (i = 0; i < nr_layers; i++) {
		for (j = 0; j < nr_exts; j++) {
			exts[j].moe_off = (i * nr_exts + j) * 4096 * 4096;
			exts[j].moe_size = 4096 * 4096;
		}

		mio_op_init(&op);
		rc = mio_composite_obj_add_extents(
			obj, layer_ids + i, nr_exts, exts, &op) ? :
		     mio_cmd_wait_on_op(&op);
		mio_op_fini(&op);
		if (rc < 0)
			break;
	}

	/*
	 * Note: the example here doesn't clean-up the extents
	 * that have been added if there is an error.
 	 */

	free(layer_ids);
	free(exts);
	return rc;
}

static int
comp_obj_del_extents(struct mio_obj *obj, int nr_layers, int nr_exts)
{
	int i;
	int j;
	int rc = 0;
	struct mio_obj_id *layer_ids;
	struct mio_obj_ext *exts;
	struct mio_op op;

	rc = comp_obj_alloc_layer_ids_n_exts(
		&obj->mo_id, &layer_ids, nr_layers, &exts, nr_exts);
	if (rc < 0)
		return rc;

	for (i = 0; i < nr_layers; i++) {
		for (j = 0; j < nr_exts; j++) {
			exts[j].moe_off = (i * nr_exts + j) * 4096 * 4096;
			exts[j].moe_size = 4096 * 4096;
		}

		mio_op_init(&op);
		rc = mio_composite_obj_del_extents(
			obj, layer_ids + i, nr_exts, exts, &op)? :
		     mio_cmd_wait_on_op(&op);
		mio_op_fini(&op);
		if (rc < 0)
			break;
	}

	free(layer_ids);
	free(exts);
	return rc;
}

static int
comp_obj_list_extents(struct mio_obj *obj, int nr_layers, int nr_exts)
{
	int i;
	int j;
	int rc = 0;
	int nr_ret_exts;
	off_t offset;
	struct mio_obj_id *layer_ids;
	struct mio_obj_ext *exts;
	struct mio_op op;

	rc = comp_obj_alloc_layer_ids_n_exts(
		&obj->mo_id, &layer_ids, nr_layers, &exts, nr_exts);
	if (rc < 0)
		return rc;

	for (i = 0; i < nr_layers; i++) {
		offset = 0; //i * nr_exts * 4096 * 4096;

		mio_op_init(&op);
		rc = mio_composite_obj_get_extents(
			obj, layer_ids + i, offset, nr_exts, exts,
			&nr_ret_exts, &op)? :
		     mio_cmd_wait_on_op(&op);
		mio_op_fini(&op);
		if (rc < 0)
			continue;

		for (j = 0; j < nr_ret_exts; j++) {
			fprintf(stderr, "layer %d: (%ld, %ld)\n",
				i, exts[j].moe_off, exts[j].moe_size);
		}
	}

	free(layer_ids);
	free(exts);
	return rc;
}

static int
comp_obj_alloc_layers(struct mio_obj_id *oid,
		      struct mio_comp_obj_layer **layers,
		      struct mio_obj_id **layer_ids, int nr_layers)
{
	if (layers == NULL || layer_ids == NULL)
		return -EINVAL;

	*layers = malloc(nr_layers * sizeof(**layers));
	*layer_ids = malloc(nr_layers * sizeof(**layer_ids));
	if (*layers == NULL || *layer_ids == NULL) {
		if (*layer_ids == NULL)
			free(*layer_ids);
		if (*layers == NULL)
			free(*layers);
		return -ENOMEM;
	}

	layer_ids_get(oid, nr_layers, *layer_ids);
	return 0;
}

static int comp_obj_add_layers(struct mio_obj *obj, int nr_layers)
{
	int i;
	int rc = 0;
	struct mio_op op;
	struct mio_comp_obj_layer *layers;
	struct mio_obj_id *layer_ids;

	rc = comp_obj_alloc_layers(&obj->mo_id, &layers, &layer_ids, nr_layers);
	if (rc < 0)
		return rc;

	/* Create objects for each layer. */
	for (i = 0; i < nr_layers; i++) {
		rc = mio_cmd_obj_touch(layer_ids + i);
		if (rc < 0)
			goto exit;
	}

	/* Add layers to the composite object. */
	for (i = 0; i < nr_layers; i++) {
		layers[i].mcol_priority = 0;
		memcpy(&layers[i].mcol_oid, layer_ids + i,
		       sizeof(struct mio_obj_id));
	}

	mio_op_init(&op);
	rc = mio_composite_obj_add_layers(obj, nr_layers, layers, &op)? :
	     mio_cmd_wait_on_op(&op);
	mio_op_fini(&op);

exit:
	free(layers);
	free(layer_ids);
	return rc;
}

static int comp_obj_del_layers(struct mio_obj *obj, int nr_layers)
{
	int i;
	int rc = 0;
	struct mio_comp_obj_layer *layers;
	struct mio_obj_id *layer_ids;
	struct mio_op op;

	rc = comp_obj_alloc_layers(&obj->mo_id, &layers, &layer_ids, nr_layers);
	if (rc < 0)
		return rc;

	for (i = 0; i < nr_layers; i++)
		/* No need to set priority for deleting layers. */
		memcpy(&layers[i].mcol_oid, layer_ids + i,
		       sizeof(struct mio_obj_id));

	mio_op_init(&op);
	rc = mio_composite_obj_del_layers(obj, nr_layers, layers, &op)? :
	     mio_cmd_wait_on_op(&op);
	mio_op_fini(&op);

	/* Delete objects for each layer. */
	for (i = 0; i < nr_layers; i++) {
		rc = mio_cmd_obj_unlink(layer_ids + i);
		if (rc < 0)
			break;
	}

	free(layers);
	free(layer_ids);
	return rc;
}

static int comp_obj_list_layers(struct mio_obj *obj)
{
	int i;
	int rc = 0;
	struct mio_comp_obj_layout layout;
	struct mio_op op;

	mio_op_init(&op);
	rc = mio_composite_obj_list_layers(obj, &layout, &op)? :
	     mio_cmd_wait_on_op(&op);
	mio_op_fini(&op);
	if (rc < 0)
		return rc;

	for (i = 0; i < layout.mlo_nr_layers; i++) {
		fprintf(stderr, "Layer %d -- ", i);
		obj_id_printf(&layout.mlo_layers[i].mcol_oid);
		fprintf(stderr, "\n");
	}
	return 0;
}

static int comp_obj_create(struct mio_obj_id *oid, struct mio_obj *obj)
{
	int rc;
	struct mio_op op;

	memset(&op, 0, sizeof op);
	rc = obj_open(oid, obj);
	if (rc == 0) {
		fprintf(stderr, "Object exists!\n");
		return -EEXIST;
	} else if (rc == -ENOENT)
		goto step_1;
	else
		return rc;

step_1:
	memset(&op, 0, sizeof op);
	rc = mio_obj_create(oid, NULL, NULL, obj, &op)? :
	     mio_cmd_wait_on_op(&op);
	if (rc < 0)
		return rc;

	/* step_2: */
	memset(&op, 0, sizeof op);
	rc = mio_composite_obj_create(oid, obj, &op)? :
	     mio_cmd_wait_on_op(&op);
	if (rc < 0) {
		mio_obj_close(obj);
		return rc;
	}

	return rc;
}

int main(int argc, char **argv)
{
	int rc;
	struct mio_cmd_obj_params comp_obj_params;
	struct mio_obj obj;

	mio_cmd_obj_args_init(argc, argv, &comp_obj_params, &comp_obj_usage);

	rc = mio_init(comp_obj_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("mio_init()", rc);
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "MIO composite example ...\n");

	fprintf(stderr, "1. Create a composite object ...");
	memset(&obj, 0, sizeof obj);
	rc = comp_obj_create(&comp_obj_params.cop_oid, &obj);
	if (rc < 0) {
		fprintf(stderr, "failed!\n");
		goto exit;
	} else
		fprintf(stderr, "success!\n");

	fprintf(stderr, "2. Add layers ...");
	rc = comp_obj_add_layers(&obj, 3);
	if (rc < 0) {
		fprintf(stderr, "failed!\n");
		goto exit;
	} else
		fprintf(stderr, "success!\n");

	rc = comp_obj_list_layers(&obj);
	if (rc < 0)
		goto exit;

	fprintf(stderr, "3. Add extents to layers ...");
	rc = comp_obj_add_extents(&obj, 3, 1);
	if (rc < 0) {
		fprintf(stderr, "failed!\n");
		goto exit;
	} else
		fprintf(stderr, "success!\n");

	fprintf(stderr, "4. List extents of layers ...\n");
	rc = comp_obj_list_extents(&obj, 3, 1);
	if (rc < 0)
		goto exit;

	fprintf(stderr, "5. Del extents of layers ...");
	rc = comp_obj_del_extents(&obj, 1, 1);
	if (rc < 0) {
		fprintf(stderr, "failed!\n");
		goto exit;
	} else
		fprintf(stderr, "success!\n");

	fprintf(stderr, "6. Del layers ...");
	rc = comp_obj_del_layers(&obj, 3);
	if (rc < 0) {
		fprintf(stderr, "failed!\n");
		goto exit;
	} else
		fprintf(stderr, "success!\n");

	mio_obj_close(&obj);

	mio_cmd_obj_unlink(&comp_obj_params.cop_oid);
exit:
	if (rc < 0)
		mio_cmd_error("mio_comp_obj example", rc);
	mio_fini();
	mio_cmd_obj_args_fini(&comp_obj_params);
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
