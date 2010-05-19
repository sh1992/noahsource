#!/bin/bash
#
# spectrograph.sh - Plot SPCAT output to postscript using gnuplot.
#
# Usage: spectrograph.sh [FILE.cat]
#
FN=$1
OUT="`perl -e '$_=$ARGV[0];s/(\.\w*)?$/.ps/;print $_' "$FN"`"
echo "$FN" '=>' "$OUT"
gnuplot <<EOF
set term postscript
set output "$OUT"
set xrange [8700:18300]
plot "$FN" using 1:(exp(\$3)) with impulses
EOF
