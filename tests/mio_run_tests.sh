#!/usr/bin/bash

#set -x

MIO_TESTS_DIR=$( cd "$(dirname "$0")" ; pwd -P )
MIO_TOP_DIR=$(echo $(dirname $MIO_TESTS_DIR) \
         | sed -r -e 's#/?tests/?$##')
MIO_UTILS_DIR=$MIO_TOP_DIR/examples/

NOW=`date "+%Y-%m-%d-%H-%M-%S"`
MIO_SANDBOX_DIR=${MIO_TESTS_DIR}/mio_test_sandbox_${NOW}
MIO_TEST_LOG=${MIO_SANDBOX_DIR}/mio_test_${NOW}.log
KEEP_MIO_SANDBOX=1

MIO_MAX_NR_OBJS=4
MIO_NR_TEST_OBJS=1

. ${MIO_TESTS_DIR}/mio_test_func.sh
. ${MIO_TESTS_DIR}/mio_obj_tests.sh
. ${MIO_TESTS_DIR}/mio_obj_io_tests.sh
. ${MIO_TESTS_DIR}/mio_kvs_tests.sh
. ${MIO_TESTS_DIR}/mio_comp_obj_tests.sh
. ${MIO_TESTS_DIR}/mio_pool_tests.sh
. ${MIO_TESTS_DIR}/mio_obj_hint_tests.sh

# Define a test array: (test, test description)
declare -A mio_test_descs
mio_test_descs[mio_obj_hint_tests]="Object hint tests"
mio_test_descs[mio_pool_tests]="Pool tests"
mio_test_descs[mio_kvs_tests]="Key/value set (KVS) tests"
mio_test_descs[mio_comp_obj_tests]="Composite object tests"
mio_test_descs[mio_obj_io_tests]="Object IO tests"
mio_test_descs[mio_obj_tests]="Object creation and deletion tests"

declare -A mio_test_params
mio_test_params[mio_obj_hint_tests]="${MIO_NR_TEST_OBJS}"
mio_test_params[mio_pool_tests]=
mio_test_params[mio_kvs_tests]=
mio_test_params[mio_comp_obj_tests]=
mio_test_params[mio_obj_io_tests]=
mio_test_params[mio_obj_tests]="${MIO_MAX_NR_OBJS}"

mio_all_tests="mio_obj_tests \
	      mio_obj_io_tests \
	      mio_comp_obj_tests \
	      mio_kvs_tests \
	      mio_pool_tests \
	      mio_pool_tests \
	      mio_obj_hint_tests"

mio_run_test()
{
	local test_idx=$1
	local test=$2

	local params=
	if [[ ! -z "${mio_test_descs[$test]}" ]]; then
		params=${mio_test_params[$test]}
		echo "[T${test_idx}] ${mio_test_descs[$test]}"
		$test $params || {
			echo "Failed"
			return 1
		}
	fi
}

mio_run_tests()
{
	tests=$@
	if [ -z "$tests" ]; then
		tests=$mio_all_tests
	fi

	local rc=0
	local test_idx=0
	echo "MIO tests start:"
	echo "Test log will be stored in $MIO_TEST_LOG."

	sandbox_init
	set -o pipefail

	for test in $tests
	do
		#echo $test_idx $test
		mio_run_test $test_idx $test 2>&1 | tee -a $MIO_TEST_LOG	
		rc=${PIPESTATUS[0]}
		if [ $rc -ne 0 ]; then
			break
		fi
		test_idx=`expr $test_idx + 1`
	done

	if [ $rc -eq 0 ]; then
		sandbox_fini
	else
		echo "Test log available at $MIO_TEST_LOG."
	fi
	return $rc
}

mio_list_tests()
{
	echo "MIO tests:"
	for i in "${!mio_test_descs[@]}"
	do
		  echo "    - $i"
	done
}

usage()
{
	cat <<.
Usage:

$ sudo mio_run_tests [-l] [tests]

Where:

-l: list all available tests.
tests: only run selected tests.
.
}

cmd="run"
selected_tests=
options="lt:"
while getopts "$options" option; do
        case "$option" in
		l)
			cmd="list"
			;;
                *)
                        usage
                        exit 1
                        ;;
        esac
done
shift $((OPTIND - 1))
selected_tests=$@

case "$cmd" in
        run)
        	mio_run_tests $selected_tests
		report_and_exit mio_run_tests $?
                ;;
        list)
		mio_list_tests
                ;;
        *)
                usage
                exit
esac

