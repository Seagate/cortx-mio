#!/usr/bin/env bash

#-----------------------------------------------------------------------------#
#                             Helper Functions for Test                       #
#-----------------------------------------------------------------------------#
die() { echo "$@" >&2; exit 1; }

report_and_exit() {
	[ $# -eq 2 ] || die "${FUNCNAME[0]}: Invalid usage"
	local name=$1
	local rc=$2

	if [ "$rc" -eq 0 ]; then
		echo "$name: test status: SUCCESS"
	else
		echo "$name: FAILURE $rc" >&2
	fi
	exit "$rc"
}

sandbox_init() {
	[ -n "${MIO_SANDBOX_DIR:-}" ] || die 'MIO_SANDBOX_DIR: unbound variable'
	rm -rf "$MIO_SANDBOX_DIR"
	mkdir -p "$MIO_SANDBOX_DIR"
	pushd "$MIO_SANDBOX_DIR" >/dev/null
}

sandbox_fini() {
	[ -n "${MIO_SANDBOX_DIR:-}" ] || die 'MIO_SANDBOX_DIR: unbound variable'
	local rc=${1:-0} # non-zero value denotes unsuccessful termination

	popd &>/dev/null || true
	if [ -z "${KEEP_MIO_SANDBOX:-}" -a "$rc" -eq 0 ]; then
		rm -r "$MIO_SANDBOX_DIR"
	fi
}

test_count=0
test_eval()
{
	local rc=0
	local cmd=$@

	printf "[$test_count] $cmd"
	eval "$cmd"
	if [ $? -ne 0 ]; then
		rc=1
	fi
	printf "\n"

	test_count=$(expr $test_count + 1)
	return $rc;
}

#-----------------------------------------------------------------------------#
#                         Object Utilities                                    #
#-----------------------------------------------------------------------------#
obj_create()
{
	local oid=$1
	local yaml=$2
	local mio_create=$MIO_UTILS_DIR/mio_touch

	test_eval "$mio_create -o $oid -y $yaml"
	if [ $? -ne 0 ]; then
		echo "Failed to create object $oid."
		return 1
	fi

	return 0
}

obj_delete()
{
	local oid=$1
	local yaml=$2
	local mio_delete=$MIO_UTILS_DIR/mio_unlink

	test_eval "$mio_delete -o $oid -y $yaml"
	if [ $? -ne 0 ]; then
		echo "Failed to delete object $oid."
		return 1
	fi

	return 0
}

obj_write()
{
	local io_size=$1
	local io_count=$2
	local oid=$3
	local yaml=$4
	local async_mode=$5
	local src=$6
	local mio_cp=$MIO_UTILS_DIR/mio_copy
	echo "$oid $yaml $async_mode"


	echo "Writing data to MIO $async_mode..."
	if [ "$async_mode" -eq 0 ]; then
		test_eval "$mio_cp -s $io_size -c $io_count -o $oid -y $yaml $src"
	else
		test_eval "$mio_cp -s $io_size -c $io_count -o $oid -a -y $yaml $src"
	fi
	if [ $? -ne 0 ]; then
		echo "Failed to write data on MIO."
		return 1
	fi

	return 0
}

obj_read()
{
	local io_size=$1
	local io_count=$2
	local oid=$3
	local yaml=$4
	local async_mode=$5
	local read_output=$6
	local mio_cat=$MIO_UTILS_DIR/mio_cat

	echo "Reading data from MIO ..."
	if [ $async_mode -eq 0 ]; then
		test_eval "$mio_cat -s $io_size -c $io_count -o $oid -y $yaml $read_output"
	else
		test_eval "$mio_cat -s $io_size -c $io_count -o $oid -a -y $yaml $read_output"
	fi
	if [ $? -ne 0 ]; then
		echo "Failed to read data from MIO."
		return 1
	fi

	return 0
}

#-----------------------------------------------------------------------------#
#                         Key-Value Set Utilities                             #
#-----------------------------------------------------------------------------#
kvs_create()
{
	local kid=$1
	local yaml=$2
	local kvs_touch=$MIO_UTILS_DIR/mio_kvs_create_set

	test_eval "$kvs_touch -k $kid -y $yaml"
	if [ $? -ne 0 ]; then
		echo "Failed to create key-value set $kid."
		return 1
	fi

	return 0
}

kvs_delete()
{
	local kid=$1
	local yaml=$2
	local kvs_rm=$MIO_UTILS_DIR/mio_kvs_del_set

	test_eval "$kvs_rm -k $kid -y $yaml"
	if [ $? -ne 0 ]; then
		echo "Failed to delete key-value set $kid."
		return 1
	fi

	return 0
}

kvs_insert_pairs()
{
	local kid=$1
	local yaml=$2
	local st_key=$3
	local nkeys=$4
	local output=$5
	local kvs_put=$MIO_UTILS_DIR/mio_kvs_insert

	test_eval "$kvs_put -k $kid -y $yaml -s $st_key -n $nkeys -l $output"
	if [ $? -ne 0 ]; then
		echo "Failed to insert pairs to key-value set $kid."
		return 1
	fi

	return 0
}

kvs_retrieve_pairs()
{
	local kid=$1
	local yaml=$2
	local st_key=$3
	local nkeys=$4
	local output=$5
	local kvs_get=$MIO_UTILS_DIR/mio_kvs_retrieve

	test_eval "$kvs_get -k $kid -y $yaml -s $st_key -n $nkeys -l $output"
	if [ $? -ne 0 ]; then
		echo "Failed to retrieve pairs to key-value set $kid."
		return 1
	fi

	return 0
}

kvs_del_pairs()
{
	local kid=$1
	local yaml=$2
	local st_key=$3
	local nkeys=$4
	local kvs_del_pairs=$MIO_UTILS_DIR/mio_kvs_del_pairs

	test_eval "$kvs_del_pairs -k $kid -y $yaml -s $st_key -n $nkeys"
	if [ $? -ne 0 ]; then
		echo "Failed to delete pairs to key-value set $kid."
		return 1
	fi

	return 0
}
