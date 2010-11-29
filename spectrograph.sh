#!/bin/bash
#
# spectrograph.sh - Plot SPCAT .CAT file using gnuplot.
#
usage() {
    cat <<EOF

Usage: spectrograph.sh [-f <format>|-p|-P] [-t <title>] [-T <title>]
                       <file>.cat [<match file>.cat]

    -f <format>, --format       Format of output plot. Supported formats are
                                "png" and "postscript".
    -p, --png                   Equivalent to --format png.
    -P, --postscript            Equivalent to --format postscript.
    -t <title>, --title         Title in legend for <file> plot.
    -T <title>, --match-title   Title in legend for <match file> plot.
    --nofitness                 Do not plot fitness (default for non-png)
    <file>.cat                  SPCAT .CAT file to plot.
    <match file>.cat            SPCAT .CAT file to show underneath main plot.

EOF
}
BINDIR=$(dirname $(readlink -f $0))

# Default options
FORMAT=png
TITLE=""
MATCHTITLE=""
NOFITNESS=""

TEMP=`getopt -o f:pPtT --long format:,png,postscript,title:match-title:,nofitness \
     -n 'spectrograph.sh' -- "$@"`
if [ $? != 0 ] ; then
    usage
    exit 1
fi
eval set -- "$TEMP"

while true; do
    case "$1" in
        -f|--format) FORMAT="$2"; shift 2 ;;
        -p|--png) FORMAT="png"; shift ;;
        -P|--postscript) FORMAT="postscript"; shift ;;
        -t|--title) TITLE="$2"; shift ;;
        -T|--match-title) MATCHTITLE="$2"; shift ;;
        --nofitness) NOFITNESS="1"; shift ;;
        --) shift; break ;;
        *) echo "Internal error"; exit 1 ;;
    esac
done

case "$FORMAT" in
    png)
        FORMAT="png size 800,600"
        EXT=.png
        ;;
    postscript)
        FORMAT=postscript
        EXT=.ps
        ;;
    *)
        echo "Invalid output format: $FORMAT"
        ;;
esac

if [ -z "$1" ]; then
    echo "spectrograph.sh: Missing filename" >&2
    usage
    exit 1
fi

# Find .CAT files
ORIGFN="$1"
shift
FN="`perl -e '$_=$ARGV[0];s/(\.\w*)?(.bz2)?$/.cat/;print $_' "$ORIGFN"`"
MATCH="$1"
shift

if [ -n "$1" -o "$MATCH" = "png" -o "$MATCH" = "postscript" ]; then
    echo "Warning: Check your command-line syntax"
fi

OUT="`perl -e '$_=$ARGV[0];s/(\.\w*)?(.bz2)?$/$ARGV[1]/;print $_' "$FN" ".int"`"
if [ -f $OUT ]; then
    # Regenerate .CAT file
    $BINDIR/spcat $FN || exit 1
else
    echo
    echo WARNING
    echo SPCAT .INT/.VAR files missing! Plotting .CAT file anyway...
    echo
fi
# Generate output plot file
[ -z "$TITLE" ] && TITLE=`basename "$FN" .cat`
[ -z "$MATCHTITLE" -a -n "$MATCH" ] && MATCHTITLE=`basename "$MATCH" .cat`

OUT="`perl -e '$_=$ARGV[0];s/(\.\w*)?(.bz2)?$/$ARGV[1]/;print $_' "$FN" "$EXT"`"
echo "$FN" '=>' "$OUT"
(
cat <<EOF
set term $FORMAT
set output "$OUT"
set xrange [8700:18300]
set lmargin at screen .1
EOF
[ -n "$MATCH" ] && cat <<EOF
set size 1,0.5

set multiplot
set origin 0.0,0.5
unset xtics
set bmargin 0
EOF
cat <<EOF
set yrange [0:*]
set key top
plot "$FN" using 1:(10**(\$3)) with impulses title "$TITLE", 0 lt 0 title ""
EOF
[ -n "$MATCH" ] && cat <<EOF

set origin 0.0,0.0
set yrange [*:0]
set xtics
set tmargin 0
set bmargin -1
set key bottom
plot "$MATCH" using 1:(-10**(\$3)) with impulses title "$MATCHTITLE", 0 lt 0 title ""

unset multiplot
EOF
) | tee /tmp/bar | gnuplot


if [ "$EXT" = ".png" -a -z "$NOFITNESS" ]; then
    LOGFN="`perl -e '$_=$ARGV[0];s/(\.\w*)?((?:.bz2)?)$/.log$2/;print $_' "$ORIGFN"`"
    if [ -f $LOGFN ]; then
        perl $BINDIR/plotfitness.pl $LOGFN
    else
        echo
        echo WARNING: No log file, not creating fitness plot
        echo
    fi
fi
