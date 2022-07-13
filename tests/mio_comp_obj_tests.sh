#!/usr/bin/env bash

comp_obj_test()
{
	local nr_objs=$1
	local oid=
	local yaml=$MIO_TESTS_DIR/mio_config.yaml
	local mio_comp_obj_cmd=$MIO_UTILS_DIR/mio_comp_obj_example

	for i in $(seq 1 "$nr_objs");
	do
		oid="1:1256$i"
		test_eval "$mio_comp_obj_cmd -o $oid -y $yaml \
			   &>> $MIO_TEST_LOG" &>> "$MIO_TEST_LOG"

		if [ $? -ne "0" ]
		then
			echo "test rc failed"
			return 1
		fi
	done

	return 0
}

mio_comp_obj_tests()
{
	comp_obj_test "$1"
	if [ $? -ne "0" ]; then
		printf "\tcomp_obj_test:  failed\n"
		return 1
	else
		printf "\tcomp_obj_test:  passed\n"
	fi

	return 0
}
