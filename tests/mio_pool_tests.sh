#!/usr/bin/env bash

pool_query_test()
{
	local pool_ids=
	local oid=
	local yaml=$MIO_TESTS_DIR/mio_config.yaml
	local mio_pool_query_cmd=$MIO_UTILS_DIR/mio_pool_query

	pool_ids=`sed -r '/^(\s*#|$)/d;' $yaml | awk '/MOTR_POOL_ID/{print $NF}'`

	for pool_id in $pool_ids;
	do
		test_eval "$mio_pool_query_cmd -p $pool_id -y $yaml \
			   &>> $MIO_TEST_LOG" &>> $MIO_TEST_LOG

		if [ $? -ne "0" ]
		then
			echo "test rc failed"
			return 1
		fi
	done

	return 0
}

mio_pool_tests()
{
	pool_query_test $1
	if [ $? -ne "0" ]; then
		printf "\tpool_query_test:  failed\n"
		return 1
	else
		printf "\tpool_query_test:  passed\n"
	fi

	return 0
}
