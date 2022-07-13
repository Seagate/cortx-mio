#!/usr/bin/env bash

kvs_create_query_delete_test()
{
	local kid="7:12345678"
	local st_key=300
	local nkeys=20
	local yaml=$MIO_TESTS_DIR/mio_config.yaml

	# Create key-value set
	kvs_create "$kid" "$yaml" &>> "$MIO_TEST_LOG"
	if [ $? -ne "0" ]
	then
		return 1
	fi

	# Insert key-value pairs
	local plog=$MIO_SANDBOX_DIR/kvs_put.log
	kvs_insert_pairs "$kid" "$yaml" "$st_key" "$nkeys" "$plog" &>> "$MIO_TEST_LOG"
	if [ $? -ne "0" ]
	then
		kvs_delete "$kid" "$yaml" &>> "$MIO_TEST_LOG"
		return 1
	fi

	# Retrieve key-value pairs
	local glog=$MIO_SANDBOX_DIR/kvs_get.log
	kvs_retrieve_pairs "$kid" "$yaml" "$st_key" "$nkeys" "$glog" &>> "$MIO_TEST_LOG"
	if [ $? -ne "0" ]
	then
		kvs_delete "$kid" "$yaml" &>> "$MIO_TEST_LOG"
		return 1
	fi

	# Delete key-value pairs
	kvs_del_pairs "$kid" "$yaml" "$st_key" "$nkeys" &>> "$MIO_TEST_LOG"
	if [ $? -ne "0" ]
	then
		kvs_delete "$kid" "$yaml" &>> "$MIO_TEST_LOG"
		return 1
	fi

	# Delete key-value set
	kvs_delete "$kid" "$yaml" &>> "$MIO_TEST_LOG"
	if [ $? -ne "0" ]
	then
		return 1
	fi

	return 0
}

mio_kvs_tests()
{
	kvs_create_query_delete_test "$1"
	if [ $? -ne "0" ]; then
		printf "\tkvs_create_query_delete_test:  failed\n"
		return 1
	else
		printf "\tkvs_create_query_delete_test:  passed\n"
	fi

	return 0
}
