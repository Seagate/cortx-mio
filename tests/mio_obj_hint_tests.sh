#!/usr/bin/env bash

obj_hint_test()
{
	local nr_objs=$1
	local oid=
	local yaml=$MIO_TESTS_DIR/mio_config.yaml
	local obj_hint_set=$MIO_UTILS_DIR/mio_hint_set
	local obj_hint_stat=$MIO_UTILS_DIR/mio_hint_stat

	for i in `seq 1 $nr_objs`;
	do
		oid="1:12348"$i""
		obj_create $oid $yaml &>> $MIO_TEST_LOG
		if [ $? -ne "0" ]
		then
			return 1
		fi

		test_eval "$obj_hint_set -o $oid -y $yaml \
			   &>> $MIO_TEST_LOG" &>> $MIO_TEST_LOG
		if [ $? -ne "0" ]
		then
			return 1
		fi

		test_eval "$obj_hint_stat -o $oid -y $yaml \
			   &>> $MIO_TEST_LOG" &>> $MIO_TEST_LOG
		if [ $? -ne "0" ]
		then
			return 1
		fi

		obj_delete $oid $yaml &>> $MIO_TEST_LOG
		if [ $? -ne "0" ]
		then
			return 1
		fi
	done

	return 0
}

mio_obj_hint_tests()
{
	obj_hint_test $1
	if [ $? -ne "0" ]; then
		printf "\tobj_hint_test:  failed\n"
		return 1
	else
		printf "\tobj_hint_test:  passed\n"
	fi

	return 0
}
