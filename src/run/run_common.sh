#!/usr/bin/env sh

main=$TOP_DIR/stat/main
args='-freq 1000 -count 10000'
ofile=/segfs/tmp/rtbench.dat

echo '# machine: ' `uname -a` > $ofile
echo '# cmdline: ' $args >> $ofile
