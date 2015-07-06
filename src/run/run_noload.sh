#!/usr/bin/env sh

TOP_DIR=`dirname $0`/..
. $TOP_DIR/run/run_common.sh

echo '# profile: noload' >> $ofile
$main $args >> $ofile

mv $ofile $TOP_DIR/dat/noload.dat
