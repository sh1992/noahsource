#!/bin/bash
#
# spectrograph.sh - Plot SPCAT output to postscript using gnuplot.
#
# Usage: spectrograph.sh [FILE.cat] [format] [title]
#
BINDIR=$(dirname $(readlink -f $0))
FN="`perl -e '$_=$ARGV[0];s/(\.\w*)?$/.cat/;print $_' "$1"`"
TITLE=""
FORMAT=postscript
EXT=.ps
if [ "$2" = "png" ]; then 
    FORMAT="png size 800,600"
    EXT=.png
fi
if [ -n "$3" ]; then    
    TITLE="title \"$3\""
fi
OUT="`perl -e '$_=$ARGV[0];s/(\.\w*)?$/$ARGV[1]/;print $_' "$FN" ".int"`"
if [ -f $OUT ]; then
    $BINDIR/spcat $FN || exit 1
else
    echo
    echo WARNING
    echo SPCAT .INT/.VAR files missing! Plotting .CAT file anyway...
    echo
fi
OUT="`perl -e '$_=$ARGV[0];s/(\.\w*)?$/$ARGV[1]/;print $_' "$FN" "$EXT"`"
echo "$FN" '=>' "$OUT"
gnuplot <<EOF
set term $FORMAT
set output "$OUT"
set xrange [8700:18300]
set yrange [0:*]
plot "$FN" using 1:(exp(\$3)) with impulses $TITLE, 0 lt 0 title ""
EOF

if [ "$EXT" = ".png" ]; then
    LOGFN="`perl -e '$_=$ARGV[0];s/(\.\w*)?$/.log/;print $_' "$1"`"
    if [ -f $LOGFN ]; then
        perl $BINDIR/plotfitness.pl $LOGFN
    else
        echo
        echo WARNING: No log file, not creating fitness plot
        echo
    fi
fi
