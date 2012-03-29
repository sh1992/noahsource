#!/bin/bash
#
# spectrograph.sh - Plot SPCAT .CAT file using gnuplot.
#
usage() {
    cat <<EOF

Usage: spectrograph.sh [-f <format>|-p|-P] [-t <title>] [-T <title>]
                       <file>.cat [...] [<match file>.cat]

    -f <format>, --format       Format of output plot. Supported formats are
                                "png", "postscript", and "latex".
    -p, --png                   Equivalent to --format png.
    -P, --postscript            Equivalent to --format postscript.
    -t <title>, --title         Title in legend for <file> plot.
    -T <title>, --match-title   Title in legend for <match file> plot.
    --nofitness                 Do not plot fitness (default for non-png)
    --range <min>:<max>         Frequency range to plot.
    <file>.cat                  SPCAT .CAT file to plot.
    <match file>.cat            SPCAT .CAT file to show underneath main plot.

EOF
}

BINDIR=$(dirname $(readlink -f $0))

convert_filename() {
    perl -I"$BINDIR" -Mgaspec -le 'print convert_filename(@ARGV)' "$1" "$2"
}

# Default options
FORMAT=png
TITLE=""
MATCHTITLE=""
XRANGE=""
OUTFILE=""
SIZE=""
NOFITNESS=0
USETITLE=0
USEMATCHTITLE=0

TEMP=`getopt -o f:pPt:T:o: \
     --long format:,png,postscript,title:,match-title:,nofitness,range:,output:,size: \
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
        -t|--title) TITLE="$2"; USETITLE=1; shift 2 ;;
        -T|--match-title) MATCHTITLE="$2"; USEMATCHTITLE=1 shift 2 ;;
        -o|--output) OUTFILE="$2"; shift 2 ;;
        --nofitness) NOFITNESS=1; shift ;;
        --range) XRANGE="$2"; shift 2 ;;
        --size) SIZE="$2"; shift 2 ;;
        --) shift; break ;;
        *) echo "Unexpected option flag: $1"; exit 1 ;;
    esac
done

case "$FORMAT" in
    png)
        [ -z "$SIZE" ] && SIZE="800,600"
        FORMAT="png size $SIZE"
        #FORMAT="png size 1600,1200 giant"
        EXT=.png
        ;;
    postscript)
        FORMAT="postscript"
        EXT=.ps
        ;;
    latex)
        [ -z "$SIZE" ] && SIZE="6in,3.99in"
        FORMAT="epslatex size $SIZE dl 2"
        EXT=.tex
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

# Find .CAT file for match
if [ $# -gt 1 ]; then
    for MATCH; do :; done  # for implicitly loops over command-line arguments
    [ "$USEMATCHTITLE" = 1 ] && MATCHTITLE=`basename "$MATCH" .cat`
fi
[ -z "$XRANGE" ] && XRANGE=8700:18300

while [ $# -gt 1 ] || [ -z "$MATCH" -a $# -eq 1 ]; do
    ORIGFN="$1"
    shift
    BASEFN="`convert_filename "$ORIGFN" ''`"    # Strip filename extension
    # Find/generate .CAT file
    FN="$BASEFN.cat"
    if [ -f "$BASEFN.int" -a -f "$BASEFN.var" ]; then
        # Regenerate .CAT file
        $BINDIR/spcat $FN || exit 1
    else
        echo
        echo WARNING
        echo SPCAT .INT/.VAR files missing! Plotting .CAT file anyway...
        echo
    fi
    # Generate output plot file
    if [ "$USETITLE" = 1 ]; then MYTITLE="$TITLE"
    else MYTITLE=`basename "$BASEFN"`; fi

    if [ -n "$OUTFILE" ]; then BASEOUT="$OUTFILE"
    else BASEOUT="$BASEFN"; fi
    OUT="$BASEOUT$EXT"

    FILTER="awk '{print substr(\$0,1,13),substr(\$0,14,8),substr(\$0,22,8)}'"
    echo "$FN" '=>' "$OUT"
    (
        cat <<EOF
        set term $FORMAT
        set output "$OUT"
        set xrange [$XRANGE]
        set x2range [$XRANGE]
        set lmargin at screen .1
EOF
        [ "$EXT" = ".tex" ] && cat <<EOF
        set rmargin at screen .98
EOF
        [ -n "$MATCH" ] && cat <<EOF

        set multiplot
        set size 1,0.5
        set origin 0.0,0.5
        unset xtics
        set x2tics format "" nomirror   # Mirror bottom tics on top plot.
        set bmargin 0
EOF
        cat <<EOF
        set yrange [0:*]
        set key top
        plot "< $FILTER $FN" using 1:(10**(\$3)) with impulses \
                             title "$MYTITLE", 0 lt 0 title ""
EOF
        [ -n "$MATCH" ] && cat <<EOF

        set origin 0.0,0.0
        set yrange [*:0]
        set xtics nomirror  # nomirror to avoid tics along central axis.
        unset x2tics
        set tmargin 0
        set bmargin -1
        set key bottom
        plot "< $FILTER $MATCH" using 1:(-10**(\$3)) with impulses \
                                title "$MATCHTITLE", 0 lt 0 title ""

        unset multiplot
EOF
    ) | tee "$BASEOUT.gnuplot" | gnuplot
    [ "$EXT" = ".tex" ] && epstopdf "$BASEOUT.eps"

    if [ "$EXT" = ".png" -a "$NOFITNESS" -ne 0 ]; then
        LOGFN=`convert_filename "$ORIGFN" .log.`
        if [ -f $LOGFN ]; then
            $BINDIR/plotfitness.pl $LOGFN
        else
            echo
            echo WARNING: No log file, not creating fitness plot
            echo
        fi
    fi
done
