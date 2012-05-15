#!/bin/sh
#
# spectrograph.sh - Plot SPCAT .CAT file using gnuplot.
#
usage() {
    cat <<EOF

Usage: spectrograph.sh [-f <format>|-p|-P] [-t <title>] [-T <title>]
                       <file>.cat [...] [<match file>.cat]

    -f <format>, --format       Format of output plot. Supported formats are
                                "png", "postscript", "latex", and "latexcolor".
    -p, --png                   Equivalent to --format png.
    -P, --postscript            Equivalent to --format postscript.
    -t <title>, --title         Title in legend for <file> plot.
    -T <title>, --match-title   Title in legend for <match file> plot.
    --nofitness                 Do not plot fitness (default for postscript)
    --range <min>:<max>         Frequency range to plot.
    --size <size>               Dimensions of output plot.
    --no-ytics                  Do not display tic marks on the y-axis
    --combine                   Combine .cat files onto one plot (except match)
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
YLABEL=""
YTICS=1
NOFITNESS=0
USETITLE=0
USEMATCHTITLE=0

TEMP=`getopt -o f:pPt:T:o: \
     --long format:,png,postscript,title:,match-title:,nofitness,range:,output:,size:,no-ytics,combine \
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
        -t|--title) if [ -z "$TITLE" ]; then TITLE="$2"; else TITLE="$TITLE
$2"; fi; USETITLE=$((USETITLE+1)); shift 2 ;; # Newline-separated
        -T|--match-title) MATCHTITLE="$2"; USEMATCHTITLE=1 shift 2 ;;
        -o|--output) OUTFILE="$2" shift 2 ;;
        --nofitness) NOFITNESS=1; shift ;;
        --range) XRANGE="$2"; shift 2 ;;
        --size) SIZE="$2"; shift 2 ;;
        --no-ytics) YTICS=0; shift 1 ;;
        --) shift; break ;;
        *) echo "Unexpected option flag: $1"; exit 1 ;;
    esac
done

ORIGFORMAT="$FORMAT"

MATCHLT=-1 # Line type for match spectrum
case "$FORMAT" in
    png)
        [ -z "$SIZE" ] && SIZE="800,600"
        FORMAT="png size $SIZE"
        #FORMAT="png size 1600,1200 giant"
        MATCHLT=1
        EXT=.png
        ;;
    postscript)
        FORMAT="postscript"
        NOFITNESS=1
        EXT=.ps
        ;;
    latex|latexcolor)
        [ -z "$SIZE" ] && SIZE="6in,3.99in"
        COLORSPEC=""; [ "$FORMAT" = latexcolor ] && COLORSPEC="color solid"
        FORMAT="epslatex size $SIZE dl 2 $COLORSPEC"
        EXT=.tex
        YLABEL="Intensity (arbitrary units)"
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

# Gnuplot does not support fixed width data files, so extract the desired
# fields and space-separate them.
FILTER="awk '{print substr(\$0,1,13),substr(\$0,14,8),substr(\$0,22,8)}'"

# Find .CAT file for match
if [ $# -gt 1 ]; then
    for MATCH; do :; done  # for implicitly loops over command-line arguments
    [ "$USEMATCHTITLE" = 0 ] && MATCHTITLE=`basename "$MATCH" .cat`
fi
[ -z "$XRANGE" ] && XRANGE=8700:18300

# Create match plot
MATCHPLOT=""
[ -n "$MATCH" ] && MATCHPLOT="
        unset label

        set origin 0.0,0.0
        set yrange [0:*] reverse
        set xtics nomirror  # nomirror to avoid tics along central axis.
        unset x2tics
        set tmargin 0
        set bmargin -1
        set key bottom
        plot \"< $FILTER $MATCH\" using 1:(10**(\$3)) with impulses \\
                                title \"$MATCHTITLE\" lt -1, 0 lt 0 title \"\"

        unset multiplot
"

# Count the number of items processed so far
INDEX=0
PLOT=""
while [ $# -gt 1 ] || [ -z "$MATCH" -a $# -eq 1 ]; do
    INDEX=$((INDEX+1))
    ORIGFN="$1"
    shift
    BASEFN="`convert_filename "$ORIGFN" ''`"    # Strip filename extension
    # Find/generate .CAT file
    FN="$BASEFN.cat"
    if [ -f "$BASEFN.int" -a -f "$BASEFN.var" ]; then
        # Regenerate .CAT file
        "$BINDIR/spcat" "$FN" || exit 1
    else
        echo
        echo WARNING
        echo SPCAT .INT/.VAR files missing! Plotting .CAT file anyway...
        echo
    fi
    # Generate output plot file
    if [ "$USETITLE" -gt 1 ]; then
        MYTITLE=`echo "$TITLE" | head -1`
        TITLE=`echo "$TITLE" | tail -n +2`
    elif [ "$USETITLE" -eq 1 ]; then MYTITLE="$TITLE"
    else MYTITLE=`basename "$BASEFN"`; fi

    if [ -n "$OUTFILE" ]; then BASEOUT="$OUTFILE"
    else BASEOUT="$BASEFN"; fi
    OUT="$BASEOUT$EXT"

    echo "$FN" '=>' "$OUT"
    PLOT="$PLOT\"< $FILTER $FN\" using 1:(10**(\$3)) with impulses \\
                             ls $INDEX title \"$MYTITLE\", 0 lt 0 title \"\""
    if [ "$INDEX" -ge "$USETITLE" ]; then (
        cat <<EOF
        set term $FORMAT
        set output "$OUT"
        set xrange [$XRANGE]
        set x2range [$XRANGE]
        set lmargin at screen .1
EOF
        [ "$ORIGFORMAT" = "latexcolor" ] && cat <<EOF
        set style line 2 lc 3
        set style line 3 lc -1
EOF
        [ "$EXT" = ".tex" ] && cat <<EOF
        set lmargin at screen .14
        set rmargin at screen .96
EOF
        [ "$YTICS" -eq 0 ] && echo "set ytics format ''" &&
            [ "$EXT" = ".tex" ] && echo "set lmargin at screen .06"
        [ -n "$MATCH" ] && cat <<EOF

        set multiplot
        set size 1,0.5
        set origin 0.0,0.5
        unset xtics
        set x2tics format "" nomirror   # Mirror bottom tics on top plot.
        set bmargin 0
EOF
        cat <<EOF
        set label "$YLABEL" at character 1,screen .5 center rotate by 90
        set yrange [0:*]
        set key top
        plot $PLOT
EOF
        echo "$MATCHPLOT"
    ) | tee "$BASEOUT.gnuplot" | gnuplot
        if [ "$?" -ne 0 ]; then
            echo "gnuplot failed."
            exit 1
        fi
        [ "$EXT" = ".tex" ] && epstopdf "$BASEOUT.eps" # Support pdfLaTeX
        PLOT=""
    else    # INDEX < USETITLE
        echo "Plot #$INDEX of $USETITLE combined plots; deferring gnuplot."
        PLOT="$PLOT,\\
             "; fi

    if [ "$NOFITNESS" -eq 0 ]; then
        LOGFN=`convert_filename "$ORIGFN" .log.`
        FITDATFN=`convert_filename "$ORIGFN" -fitness.dat`
        if [ -f "$LOGFN" ] || [ -f "$FITDATFN" ]; then
            $BINDIR/plotfitness.pl "$LOGFN"
        else
            echo
            echo WARNING: No log file, not creating fitness plot
            echo
        fi
    fi
done
