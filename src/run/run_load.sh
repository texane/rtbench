#!/usr/bin/env sh

TOP_DIR=`dirname $0`/..
. $TOP_DIR/run/run_common.sh

echo '# profile: load' >> $ofile

$TOP_DIR/load/main &
LOAD_PID=$!

$main $args >> $ofile

kill -2 $LOAD_PID

mv $ofile $TOP_DIR/dat/load.dat
