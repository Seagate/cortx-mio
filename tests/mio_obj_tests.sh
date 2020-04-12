#!/usr/bin/env bash

obj_create_delete_test()
{
	local nr_objs=$1
	local oid=
	local yaml=$MIO_TESTS_DIR/mio_config.yaml

	for i in `seq 1 $nr_objs`;
	do
		oid="1:12348"$i""
		obj_create $oid $yaml &>> $MIO_TEST_LOG
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

mio_obj_tests()
{
	obj_create_delete_test $1
	if [ $? -ne "0" ]; then
		printf "\tio_create_delete_test:  failed\n"
		return 1
	else
		printf "\tio_create_delete_test:  passed\n"
	fi

	return 0
}
