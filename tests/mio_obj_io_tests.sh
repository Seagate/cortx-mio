dd_file()
{
	local io_sizesize=$1
	local io_count=$2
	local file=$3

	test_eval "dd if=/dev/urandom of=$file bs=$io_size count=$io_count"
	if [ $? -ne 0 ]; then
		echo "Failed to create local input file."
		return 1
	fi

	return 0
}

obj_cleanup()
{
	local oid=$1
	local yaml=$2

	# Clean up the object
	obj_delete $oid $yaml &>> $MIO_TEST_LOG
	if [ $? -ne "0" ]
	then
		echo -n "Failed to delete object $oid "
		return 1
	fi

	return 0;
}

obj_write_read_cmp()
{
	local oid="1:12345678"
	local io_size=$1
	local io_count=$2
	local async_mode=$3
	local yaml=$4
	local fsize=`expr $io_size \* $io_count`
	local local_input=$MIO_SANDBOX_DIR/$fsize.in
	local local_output=$MIO_SANDBOX_DIR/$fsize.out

	dd_file $io_size $io_count $local_input &>> $MIO_TEST_LOG
	if [ $? -ne 0 ]; then
		echo "Failed to create local input file."
		return 1
	fi

	obj_write $io_size $io_count $oid $yaml $async_mode $local_input &>> $MIO_TEST_LOG
	if [ $? -ne 0 ]; then
		obj_cleanup $oid $yaml
		return 1
	fi

	obj_read $io_size $io_count $oid $yaml $async_mode $local_output &>> $MIO_TEST_LOG
	if [ $? -ne 0 ]; then
		obj_cleanup $oid $yaml
		return 1
	fi

	if ! cmp $local_input $local_output
	then
		echo -n "Failed: data written and data read from MIO "
		echo    "are not same."
		obj_cleanup $oid $yaml
		return 1
	fi

	obj_cleanup $oid $yaml &>> $MIO_TEST_LOG
	return 0
}

io_test_with_sizes_of_multi_4KB()
{
	local async_mode=$1
	local yaml=$MIO_TESTS_DIR/mio_config.yaml

	for io_size in 4 16
	do
		io_size=`expr $io_size \* 1024`

		for io_count in 1 16
		do
			obj_write_read_cmp $io_size $io_count $async_mode $yaml &>> $MIO_TEST_LOG
			if [ $? -ne "0" ]
			then
				return 1
			fi
		done
	done

	return 0
}

io_test_with_sizes_of_multi_non_4KB()
{
	local async_mode=$1
	local yaml=$MIO_TESTS_DIR/mio_config.yaml

	for io_size in 3 31
	do
		io_size=`expr $io_size \* 1024`

		for io_count in 1 16
		do
			obj_write_read_cmp $io_size $io_count $async_mode $yaml &>> $MIO_TEST_LOG
			if [ $? -ne "0" ]
			then
				return 1
			fi
		done
	done

	return 0
}


io_test_in_async()
{
	local async=1

	io_test_with_sizes_of_multi_4KB $async
	return $?
}

io_test_with_multi_procs()
{
	local oids=
	local pids=
	local yaml=
	local io_size=$((2 * 4096))
	local io_count=4
	local fsize=`expr $io_size \* $io_count`
	local local_input=$MIO_SANDBOX_DIR/$fsize.in
	local mio_cp=$MIO_UTILS_DIR/mio_copy

	dd_file $io_size $io_count $local_input &>> $MIO_TEST_LOG

	# Spawn parallel mio_cp's
	for i in `seq 1 2`
	do
		oids[$i]="1:100"$i"100"
		yaml=$MIO_TESTS_DIR/mio_config_p$i.yaml
		test_eval "$mio_cp -s $io_size -c $io_count -o ${oids[$i]} -y $yaml \
		      $local_input &>> $MIO_TEST_LOG &" &>> $MIO_TEST_LOG
		pids[$i]=$!
	done

	# Wait for IO to complete
	for i in `seq 1 2`
	do
		wait ${pids[$i]}
	done

	# Cleanup objects
	for i in `seq 1 2`
	do
		obj_cleanup ${oids[$i]} $yaml
	done

	return 0
}

io_test_with_multi_threads()
{
	local rc=0
	local oid="0:12345678"
	local io_size=4096
	local io_count=16
	local nthreads=2
	local nobjs=10
	local yaml=$MIO_TESTS_DIR/mio_config.yaml
	local mio_rw_threads=$MIO_UTILS_DIR/mio_rw_threads

	test_eval "$mio_rw_threads -s $io_size -c $io_count -o $oid -t $nthreads \
	      -n $nobjs -y $yaml &>> $MIO_TEST_LOG" &>> $MIO_TEST_LOG
	if [ $? -ne 0 ]; then
		rc=1
	fi

	return $rc
}

io_test_obj_rw_with_lock()
{
	local rc=0
	local oid="0:12345678"
	local io_size=4096
	local io_count=16
	local nthreads=4
	local yaml=$MIO_TESTS_DIR/mio_config.yaml
	local mio_rw_lock=$MIO_UTILS_DIR/mio_rw_lock

	test_eval "$mio_rw_lock -s $io_size -c $io_count -o $oid -t $nthreads \
	      -y $yaml &>> $MIO_TEST_LOG" &>> $MIO_TEST_LOG
	if [ $? -ne 0 ]; then
		rc=1
	fi

	return $rc
}

mio_obj_io_tests()
{
	io_test_with_sizes_of_multi_4KB 0
	if [ $? -eq "0" ]; then
		printf "\tio_test_with_sizes_of_multi_4KB:  passed\n"
	else
		printf "\tio_test_with_sizes_of_multi_4KB:  failed\n"
		return 1
	fi

	io_test_with_sizes_of_multi_non_4KB 0
	if [ $? -eq "0" ]; then
		printf "\tio_test_with_sizes_of_multi_non_4KB:  passed\n"
	else
		printf "\tio_test_with_sizes_of_multi_non_4KB:  failed\n"
		return 1
	fi

	io_test_in_async
	if [ $? -eq "0" ]; then
		printf "\tio_test_in_async:  passed\n"
	else
		printf "\tio_test_with_varied_sizes:  failed\n"
		return 1
	fi

	io_test_obj_rw_with_lock
	if [ $? -eq "0" ]; then
		printf "\tio_test_obj_rw_with_lock:  passed\n"
	else
		printf "\tio_test_obj_rw_with_lock:  failed\n"
		return 1
	fi


	io_test_with_multi_threads
	if [ $? -eq "0" ]; then
		printf "\tio_test_with_multi_threads:  passed\n"
	else
		printf "\tio_test_with_multi_threads:  failed\n"
		return 1
	fi

	io_test_with_multi_procs
	if [ $? -eq "0" ]; then
		printf "\tio_test_with_multi_procs:  passed\n"
	else
		printf "\tio_test_with_multi_procs:  failed\n"
		return 1
	fi

	return 0
}
