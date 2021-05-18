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
#include <errno.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <asm/byteorder.h>
#include <editline/readline.h>

#include "obj.h"
#include "helpers.h"

enum hsm_action {
	HSM_INVALID_ACT = -1,
	HSM_HELP = 0,
	HSM_CREATE,
	HSM_WRITE,
	HSM_READ,
	HSM_MOVE,
	HSM_SHOW,
	HSM_WORKLOAD,
	HSM_SET_HOT_OBJ_THLD,
	HSM_SET_COLD_OBJ_THLD,
	HSM_GET_OBJ_THLDS
};

static struct mio_cmd_obj_params hsm_params;
static char *hsm_log = "./hsm_log.mio";

static void hsm_usage(FILE *file, char *prog_name)
{
	fprintf(file, "Usage: %s [OPTION]... SOURCE\n"
"HSM Shell.\n"
"\n"
"  -o, --object         OID       ID of the mero object\n"
"  -s, --block-size     INT       block size in bytes or with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -c, --block-count    INT       number of blocks to copy, can give with " \
				 "suffix b/k/m/g/K/M/G\n"
"  -n                   INT       The number of objects\n"
"  -y, --mio_conf_file            MIO YAML configuration file\n"
"  -h, --help                     shows this help text and exit\n"
"\n"
" actions:\n"
"    create \n"
"    write <index> <number of objects> <number of blocks>\n"
"    read <index> <number of objects> <number of blocks>\n"
"    move <index> <number of objects> \n"
"    show <index> <number of objects> \n"
"    set_hot_thld <hot object threshold> \n"
"    set_cold_thld <cold object threshold> \n"
"    get_thlds \n"
"    workload "
, prog_name);
}

static void hsm_action_usage()
{
	fprintf(stderr,
" actions:\n"
"    create \n"
"    write <index> <number of objects> <number of blocks>\n"
"    read <index> <number of objects> <number of blocks>\n"
"    move <index> <number of objects> \n"
"    show <index> <number of objects> \n"
"    set_hot_thld <hot object threshold> \n"
"    set_cold_thld <cold object threshold> \n"
"    get_thlds \n"
"    workload \n");
}

static void
hsm_make_oid(struct mio_obj_id *oid, int idx)
{
        uint64_t u1;
        uint64_t u2;
        uint64_t n1;
        uint64_t n2;
	struct mio_obj_id *st_oid = &hsm_params.cop_oid;

        memcpy(&u1, st_oid->moi_bytes, sizeof u1);
        memcpy(&u2, st_oid->moi_bytes + sizeof u1, sizeof u2);
	u1 = __be64_to_cpu(u1);
	u2 = __be64_to_cpu(u2);

        n1 = u1 + idx;
        n2 = u2;
	n1 = __cpu_to_be64(n1);
	n2 = __cpu_to_be64(n2);
        memcpy(oid->moi_bytes, &n1, sizeof n1);
        memcpy(oid->moi_bytes + sizeof n1, &n2, sizeof n2);
}

static void
hsm_make_tmp_oid(struct mio_obj_id *oid)
{
        uint64_t u1;
        uint64_t u2;
        uint64_t n1;
        uint64_t n2;
	struct mio_obj_id *st_oid = &hsm_params.cop_oid;

        memcpy(&u1, st_oid->moi_bytes, sizeof u1);
        memcpy(&u2, st_oid->moi_bytes + sizeof u1, sizeof u2);
	u1 = __be64_to_cpu(u1);
	u2 = __be64_to_cpu(u2);

        n1 = u1;
        n2 = 0x1000;
	n1 = __cpu_to_be64(n1);
	n2 = __cpu_to_be64(n2);
        memcpy(oid->moi_bytes, &n1, sizeof n1);
        memcpy(oid->moi_bytes + sizeof n1, &n2, sizeof n2);
}

static int hsm_create_objs()
{
	int i;
	int rc = 0;
	struct mio_obj_id oid;

	printf("[hsm_create_objs] create %d objects ... \n", hsm_params.cop_nr_objs);
	for (i = 0; i < hsm_params.cop_nr_objs; i++) {
		hsm_make_oid(&oid, i);
		rc = mio_cmd_obj_touch(&oid);
		if (rc < 0)
			break;
	}

	if (rc < 0) {
		for (i = 0; i < hsm_params.cop_nr_objs; i++) {
			hsm_make_oid(&oid, i);
			mio_cmd_obj_unlink(&oid);
		}
	}

	return rc;
}

static int hsm_write_objs(int idx, int nr_objs, int blk_cnt)
{
	int i;
	int rc;
	struct mio_obj_id oid;
        struct timeval stv;
	struct timeval etv;
	uint64_t wtime;

	if (idx >= hsm_params.cop_nr_objs)
		return -EINVAL;

	if (idx + nr_objs > hsm_params.cop_nr_objs)
		nr_objs = hsm_params.cop_nr_objs - idx;

	gettimeofday(&stv, NULL);
	for (i = idx; i < idx + nr_objs; i++) {
		hsm_make_oid(&oid, i);
		rc = mio_cmd_obj_write(NULL, NULL, &oid,
				hsm_params.cop_block_size, blk_cnt);
		if (rc < 0)
			break;
	}
	gettimeofday(&etv, NULL);

	wtime = (etv.tv_sec - stv.tv_sec) * 1000000 + etv.tv_usec - stv.tv_usec; 
	printf("WRITE Time: %lu secs %lu usecs\n",
		wtime / 1000000, wtime % 1000000);

	return rc;
}

static int hsm_read_objs(int idx, int nr_objs, int blk_cnt)
{
	int i;
	int rc;
	struct mio_obj_id oid;
        struct timeval stv;
	struct timeval etv;
	uint64_t rtime;

	if (idx >= hsm_params.cop_nr_objs)
		return -EINVAL;

	if (idx + nr_objs > hsm_params.cop_nr_objs)
		nr_objs = hsm_params.cop_nr_objs - idx;

	gettimeofday(&stv, NULL);
	for (i = idx; i < nr_objs; i++) {
		hsm_make_oid(&oid, i);
		rc = mio_cmd_obj_read(&oid, hsm_log,
				      hsm_params.cop_block_size, blk_cnt);
		if (rc < 0)
			break;
	}
	gettimeofday(&etv, NULL);

	rtime = (etv.tv_sec - stv.tv_sec) * 1000000 + etv.tv_usec - stv.tv_usec; 
	printf("READ Time: %lu secs %lu usecs\n",
		rtime / 1000000, rtime % 1000000);


	return rc;
}

static int
hsm_obj_hint_stat(struct mio_obj_id *oid, struct mio_cmd_obj_hint *chint)
{
        int rc;
        struct mio_obj obj;

        memset(&obj, 0, sizeof obj);
        rc = mio_cmd_obj_open(oid, &obj);
        if (rc < 0)
                return rc;

        rc = mio_obj_hint_get(&obj, chint->co_hkey, &chint->co_hvalue);
        if (rc < 0)
                fprintf(stderr, "Can't get %s's value, rc = %d\n",
                        mio_hint_name(MIO_HINT_SCOPE_OBJ, chint->co_hkey), rc);

        mio_cmd_obj_close(&obj);
        return rc;
}

static int
hsm_obj_pool_id(struct mio_obj_id *oid, struct mio_pool_id *pool_id)
{
        int rc;
        struct mio_obj obj;

        memset(&obj, 0, sizeof obj);
        rc = mio_cmd_obj_open(oid, &obj);
        if (rc < 0)
                return rc;

	rc = mio_obj_pool_id(&obj, pool_id);
        mio_cmd_obj_close(&obj);
        return rc;
}

enum {
	HSM_OBJ_MOVE = 0,
	HSM_OBJ_NOT_MOVE = 1,
};

static int
hsm_obj_if_move(struct mio_obj_id *oid, uint64_t hotness)
{
        int rc;
	struct mio_pool_id pool_id;
	struct mio_pool_id new_pool_id;

	rc = hsm_obj_pool_id(oid, &pool_id);
	if (rc < 0)
		return rc;

	new_pool_id = mio_obj_hotness_to_pool_id(hotness);
	if (mio_obj_pool_id_cmp(&pool_id, &new_pool_id))
		rc = HSM_OBJ_NOT_MOVE;
	else
		rc = HSM_OBJ_MOVE;

        return rc;
}

static int hsm_move_one(struct mio_obj_id *oid)
{
	int rc;
	struct mio_obj_id tmp_oid;
	struct mio_cmd_obj_hint chint;

	/*
	 * 0. Get the hotness of the object. Then use it to check which
	 * pool the old object is moved to. If the pool doesn't change,
	 * skip moving data and return.
	 */
	memset(&chint, 0, sizeof(chint));
	chint.co_hkey = MIO_HINT_OBJ_HOT_INDEX;
	rc = hsm_obj_hint_stat(oid, &chint);
	if (rc < 0)
		return rc;

	if (hsm_obj_if_move(oid, chint.co_hvalue) == HSM_OBJ_NOT_MOVE) {
		printf("Object stays in the same pool, not moving :)\n");
		return 0;
	}

	/* 1. Create a tmp object and copy data to it. */
	hsm_make_tmp_oid(&tmp_oid);
	rc = mio_cmd_obj_copy(oid, NULL, &tmp_oid,
			      hsm_params.cop_block_size, NULL);
	if (rc < 0)
		return rc;

	/* 2. Delete the original object. */
	rc = mio_cmd_obj_unlink(oid);
	if (rc < 0)
		return rc;

	/* 3. Create a new object with the same `to` id but in different pool. */
	rc = mio_cmd_obj_copy(&tmp_oid, NULL, oid,
			      hsm_params.cop_block_size, &chint);
	if (rc < 0) {
		fprintf(stderr, "Failed to move object back to new pool!");
		return rc;
	}

	rc = mio_cmd_obj_unlink(&tmp_oid);
	return rc;
}

static int hsm_move_objs(int idx, int nr_objs)
{
	int i;
	int rc = 0;
	struct mio_obj_id oid;

	if (idx >= hsm_params.cop_nr_objs)
		return -EINVAL;

	if (idx + nr_objs > hsm_params.cop_nr_objs)
		nr_objs = hsm_params.cop_nr_objs - idx;

	for (i = 0; i < nr_objs; i++) {
		hsm_make_oid(&oid, idx + i);

		rc = hsm_move_one(&oid);
		if (rc < 0)
			break;
	}

	return rc;
}

static int hsm_show_objs(int idx, int nr_objs)
{
	int i;
	int rc = 0;
	struct mio_obj_id oid;
	struct mio_pool_id pool_id;

	if (idx >= hsm_params.cop_nr_objs)
		return -EINVAL;

	if (idx + nr_objs > hsm_params.cop_nr_objs)
		nr_objs = hsm_params.cop_nr_objs - idx;

	printf("Object     POOL_ID\n");
	for (i = 0; i < nr_objs; i++) {
		hsm_make_oid(&oid, idx + i);

		rc = hsm_obj_pool_id(&oid, &pool_id);
		if (rc < 0)
			break;
		printf("[Object %d] (%"PRIx64":%"PRIx64")\n",
			i, pool_id.mpi_hi, pool_id.mpi_lo);

	}

	return rc;
}


/*
 * Set the hot and cold object thresholds.
 */
static int hsm_set_obj_thld(int argc, char **argv, bool set_hot_thld)
{
	int thld;

	if (optind > argc - 1) {
		hsm_action_usage();
		return -EINVAL;
	}
	thld = atoi(argv[optind++]); 

	if (set_hot_thld)
		mio_sys_hint_set(MIO_HINT_HOT_OBJ_THRESHOLD, thld);
	else
		mio_sys_hint_set(MIO_HINT_COLD_OBJ_THRESHOLD, thld);

	return 0;
}

static int hsm_get_obj_thlds()
{
	int rc;
	uint64_t hot_thld;
	uint64_t cold_thld;

	rc = mio_sys_hint_get(MIO_HINT_HOT_OBJ_THRESHOLD, &hot_thld)? :
	     mio_sys_hint_get(MIO_HINT_COLD_OBJ_THRESHOLD, &cold_thld);
	if (rc < 0) {
		printf("object hot/cold thresholds haven't been set yet.\n");
		return rc;
	}

	printf("Object Hot Threshold: %lu\n", hot_thld);
	printf("Object Cold Threshold: %lu\n", cold_thld);
	return 0;
}
enum {
	HSM_HOTEST_INDEX = 16
};

static int hsm_workload_generate()
{
	int i;
	int j;
	int rc = 0;
	uint32_t *freqs;
	int nr_objs;
	int nr_ops;
	int adjusted_nr_ops;
	uint32_t which;
	uint32_t rand;
	uint32_t sigma = 0;
	struct mio_obj_id oid;
	struct mio_cmd_obj_hint chint;

	nr_ops = 128;
	nr_objs = hsm_params.cop_nr_objs;
	freqs = malloc(sizeof(*freqs) * nr_objs);
	if (freqs == NULL)
		return -ENOMEM;

	/* Assign each object a frequency index. */
	for (i = 0; i < nr_objs; i++) {
		rand = (i + 1) * mio_cmd_random(HSM_HOTEST_INDEX);
		freqs[i] = rand;
		sigma += rand;
	}

	adjusted_nr_ops = 0;
	for (i = 0; i < nr_objs; i++) {
		freqs[i] = freqs[i] * nr_ops / sigma;
		adjusted_nr_ops += freqs[i];
	}
	nr_ops = adjusted_nr_ops;

	/* Choose an object according to frequency. */
	for (i = 0; i < nr_ops; i++) {
		which = mio_cmd_random(nr_ops);

		sigma = 0;
		for (j = 0; j < nr_objs; j++) {
			if (which < sigma + freqs[j])
				break;
			sigma += freqs[j];
		}

		hsm_make_oid(&oid, j);
		rc = mio_cmd_obj_read(&oid, hsm_log, hsm_params.cop_block_size,
				      hsm_params.cop_block_count);
		if (rc < 0)
			break;
	}
	if (rc < 0)
		goto exit;

	for (i = 0; i < nr_objs; i++) {
		memset(&chint, 0, sizeof(chint));
		chint.co_hkey = MIO_HINT_OBJ_HOT_INDEX;
		hsm_make_oid(&oid, i);
		rc = hsm_obj_hint_stat(&oid, &chint);

		printf("OBJ%d Hotness:", i);
		for (j = 0; j < chint.co_hvalue / 5; j++)
			printf("==");
		printf("\n");
	}

exit:
	free(freqs);
	return rc;
}

static enum hsm_action hsm_get_action(int argc, char **argv)
{
	enum hsm_action action;
	const char *action_str;

	action_str = argv[optind];
	if (!strcmp(action_str, "help"))
		action = HSM_HELP;
	else if (!strcmp(action_str, "create"))
		action = HSM_CREATE;
	else if (!strcmp(action_str, "workload"))
		action = HSM_WORKLOAD;
	else if (!strcmp(action_str, "write"))
		action = HSM_WRITE;
	else if (!strcmp(action_str, "read"))
		action = HSM_READ;
	else if (!strcmp(action_str, "move"))
		action = HSM_MOVE;
	else if (!strcmp(action_str, "show"))
		action = HSM_SHOW;
	else if (!strcmp(action_str, "set_hot_thld"))
		action = HSM_SET_HOT_OBJ_THLD;
	else if (!strcmp(action_str, "set_cold_thld"))
		action = HSM_SET_COLD_OBJ_THLD;
	else if (!strcmp(action_str, "get_thlds"))
		action = HSM_GET_OBJ_THLDS;
	else
		action = HSM_INVALID_ACT;

	optind++;
	return action;
}

static int hsm_get_io_args(int argc, char **argv,
			   int *obj_idx, int *nr_objs, int *blk_cnt)
{
	if (optind >= argc - 1) {
		hsm_action_usage();
		return -EINVAL;
	}

	*obj_idx = atoi(argv[optind++]); 
	*nr_objs = atoi(argv[optind++]);
	if (blk_cnt != NULL) {
		if (optind >= argc)
			*blk_cnt = 0;
		else
			*blk_cnt = atoi(argv[optind++]);
	}
	return 0;
}

static int hsm_shell_run_cmd(int argc, char **argv)
{
	int rc = 0;
	int idx;
	int nr_objs;
	int blk_cnt;
	enum hsm_action action;

	action = hsm_get_action(argc, argv);
	if (action == HSM_INVALID_ACT || action == HSM_HELP) {
		hsm_action_usage();
		if (action == HSM_INVALID_ACT)
			return -EINVAL;
		else
			return 0;
	}

	switch (action) {
	case HSM_CREATE:
		rc = hsm_create_objs();
		break;
	case HSM_WORKLOAD:
		rc = hsm_workload_generate();
		break;
	case HSM_WRITE:
		rc = hsm_get_io_args(argc, argv, &idx, &nr_objs, &blk_cnt)? :
		     hsm_write_objs(idx, nr_objs, blk_cnt);
		break;
	case HSM_READ:
		rc = hsm_get_io_args(argc, argv, &idx, &nr_objs, &blk_cnt)? :
		     hsm_read_objs(idx, nr_objs, blk_cnt);
		break;
	case HSM_MOVE:
		rc = hsm_get_io_args(argc, argv, &idx, &nr_objs, NULL)? :
		     hsm_move_objs(idx, nr_objs);
		break;
	case HSM_SHOW:
		rc = hsm_get_io_args(argc, argv, &idx, &nr_objs, NULL)? :
		     hsm_show_objs(idx, nr_objs);
		break;
	case HSM_SET_HOT_OBJ_THLD:
		rc = hsm_set_obj_thld(argc, argv, true);
		break;
	case HSM_SET_COLD_OBJ_THLD:
		rc = hsm_set_obj_thld(argc, argv, false);
		break;
	case HSM_GET_OBJ_THLDS:
		rc = hsm_get_obj_thlds();
		break;
	default:
		break;
	}

	return rc;
}

#define SH_TOK_BUFSIZE 64
#define SH_TOK_DELIM " \t\r\n\a"
char** hsm_shell_split_line(char *line, int *argc)
{
	int bufsize = SH_TOK_BUFSIZE;
	int position = 0;
	char **tokens;
	char *token;

	tokens = malloc(bufsize * sizeof(char*));
	if (!tokens) {
		fprintf(stderr, "mio_hsm: allocation error\n");
		return NULL;
	}

	/* set first arg add command name to be getopt compliant */
	tokens[0] = "c0hsm";
	position++;

	token = strtok(line, SH_TOK_DELIM);
	while (token != NULL) {
		tokens[position] = token;
		position++;

		if (position >= bufsize) {
			bufsize += SH_TOK_BUFSIZE;
			tokens = realloc(tokens, bufsize * sizeof(char*));
			if (!tokens) {
				fprintf(stderr, "c0hsm: allocation error\n");
				return NULL;
			}
		}

		token = strtok(NULL, SH_TOK_DELIM);
	}
	tokens[position] = NULL;
	*argc = position;
	return tokens;
}

static int hsm_shell_loop()
{
	char *line;
	int argc;
	char **argv;

	using_history();
	while (1) {
		optind = 1;
		line = readline("c0hsm> ");
		if (!line)
			break;
		if (strlen(line) > 0)
			add_history(line);
		argv = hsm_shell_split_line(line, &argc);

		if (argc > 1) {
			if (!strcmp(argv[1], "quit"))
				break;
			hsm_shell_run_cmd(argc, argv);
		}

		free(argv);
		free(line);
	}
	return 0;
}

int main(int argc, char **argv)
{
	int rc;

	mio_cmd_obj_args_init(argc, argv, &hsm_params, &hsm_usage);

	rc = mio_init(hsm_params.cop_conf_fname);
	if (rc < 0) {
		mio_cmd_error("Initialising MIO failed", rc);
		exit(EXIT_FAILURE);
	}

	rc = hsm_shell_loop();
	if (rc < 0)
		mio_cmd_error("Writing object failed", rc);

	mio_fini();
	mio_cmd_obj_args_fini(&hsm_params);
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
