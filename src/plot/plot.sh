#!/usr/bin/env sh

dat=$1
png=`dirname $dat`/`basename $dat`.png
tmp=`tempfile`

> $tmp cat <<EOF
reset
set term png truecolor
set output "$png"
set title 'IRQ handling latency'
set xrange [0:]
set xlabel 'IRQ handling delay (us)'
set ylabel 'IRQ count'
set grid
set boxwidth 0.95 relative
set style fill transparent solid 0.5 noborder
plot "$dat" u 1:2 w boxes lc rgb"green" notitle
EOF

gnuplot $tmp
rm $tmp
