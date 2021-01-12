#!/usr/bin/bash

#set -x

MIO_TESTS_DIR=$( cd "$(dirname "$0")" ; pwd -P )
echo $MIO_TESTS_DIR
MIO_TOP_DIR=$(echo $(dirname $MIO_TESTS_DIR) \
         | sed -r -e 's#/?tests/?$##')
echo $MIO_TOP_DIR
MIO_UTILS_DIR=$MIO_TOP_DIR/examples/
echo $MIO_UTILS_DIR

NOW=`date "+%Y-%m-%d-%H-%M-%S"`
MIO_SANDBOX_DIR=${MIO_TESTS_DIR}/mio_test_sandbox_${NOW}
MIO_TEST_LOG=${MIO_SANDBOX_DIR}/mio_test_${NOW}.log
KEEP_MIO_SANDBOX=1

MIO_MAX_NR_OBJS=4

. ${MIO_TESTS_DIR}/mio_test_func.sh
. ${MIO_TESTS_DIR}/mio_obj_tests.sh
. ${MIO_TESTS_DIR}/mio_obj_io_tests.sh
. ${MIO_TESTS_DIR}/mio_kvs_tests.sh
. ${MIO_TESTS_DIR}/mio_comp_obj_tests.sh
. ${MIO_TESTS_DIR}/mio_pool_tests.sh
. ${MIO_TESTS_DIR}/mio_obj_hint_tests.sh

mio_run_tests()
{
	echo "[T1] Object creation and deletion tests"
	mio_obj_tests $MIO_MAX_NR_OBJS || {
		echo "Failed"
		return 1
	}

	echo "[T2] Object IO tests"
	mio_obj_io_tests || {
		echo "Failed"
		return 1
	}

	echo "[T3] KVS tests"
	mio_kvs_tests || {
		echo "Failed"
		return 1
	}

	echo "[T4] Composite object tests"
	mio_comp_obj_tests || {
		echo "Failed"
		return 1
	}

	echo "[T5] Pool tests"
	mio_pool_tests || {
		echo "Failed"
		return 1
	}

	echo "[T6] Object hint tests"
	mio_obj_hint_tests 1 || {
		echo "Failed"
		return 1
	}
}

main()
{
	local rc=0
	echo "MIO tests start:"
	echo "Test log will be stored in $MIO_TEST_LOG."

	sandbox_init

	set -o pipefail

	mio_run_tests 2>&1 | tee -a $MIO_TEST_LOG
	rc=${PIPESTATUS[0]}

	if [ $rc -eq 0 ]; then
		sandbox_fini
	else
		echo "Test log available at $MIO_TEST_LOG."
	fi
	return $rc
}

main
report_and_exit mio_run_tests $?
