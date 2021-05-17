#!/bin/bash

dir=$( cd "$(dirname "$0")" ; pwd -P )

addb=$1
if [ ! -f $addb ]; then
	echo "Can't find ADDB dump file!"
	echo "Usage: $ mio_addb_parser addb_dump_file"
	exit 1
fi

# Filter out those MIO records.
tmp=.tmp.txt
mio_addb_id=30000
echo "Filter MIO records ..."
grep -E $mio_addb_id $addb > $tmp

# Parse MIO records.
parser=$dir/mio_telemetry_parser
if [ ! -f $parser ]; then
	echo "Can't find MIO telemetry parser!"
	exit 1
fi
eval "$parser $tmp 'addb' >> $addb"
if [ $? -ne 0 ]; then
	echo "Failed to parse MIO telemetry records!"
	exit 1
fi
