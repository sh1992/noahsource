#!/usr/bin/perl
#
# plotfitness.pl - Plot a graph of fitness as a function of generation.
# Takes ga-spectroscopy log files as input.
#

use FindBin;
use lib $FindBin::Bin;
use gaspec;
use warnings;
use strict;

die "Usage: $0 <files>\n" unless @ARGV;
foreach my $fn ( @ARGV ) {
    *F = open_compressed($fn);
    my @gens = ();
    my $hasdynmut = 0;
    my $ytop = 0;
    my ($sectot, $secgen) = (0, 0);
    while (<F>) {
        if ( m/DYNM (\d+) (?:AVG\s+(-?[.0-9]+))?(\s+A\s+(-?[.0-9]+)\s+B\s+(-?[.0-9]+)\s+D\s+(-?[.0-9]+)\s+R\s+(-?[.0-9]+))?/ ) {
            my ($gen,$mean,$x,$a,$b,$d,$r) = ($1,$2,$3,$4,$5,$6,$7);
            $hasdynmut = 1 if $x;
            $gens[$gen]{avg} = $mean if defined($mean);
            $gens[$gen]{leading} = $a;
            $gens[$gen]{trailing} = $b;
            $gens[$gen]{difference} = $d;
            $gens[$gen]{mutationrate} = $r;
        }
        elsif ( m/BEST (\d+).+orig\s+(-?[.0-9]+)/ ) {
            my ($gen,$max) = ($1,$2);
            $gens[$gen]{gen} = $gen;
            $gens[$gen]{best} = $max;
            $ytop = int($max);
        }
        elsif ( m/AVRG (\d+)\s+(-?[.0-9]+)/ ) { $gens[$1]{avg} = $2 } # Old style
        elsif ( m/Took ([.0-9]+) seconds \(([.0-9]+) sec\/gen\)/ )
            { $sectot = $1; $secgen = $2 }
    }
    $ytop++;

    my $outfn = convert_filename($fn,'-fitness.png');
    my $datfn = convert_filename($fn,'-fitness.dat');
    my $gpfn = convert_filename($fn,'-fitness.gnuplot');

    open DAT, '>', $datfn or die "Cannot open output file $datfn: $!";
    my @keys = qw/gen avg best/;
    push @keys, qw/leading trailing mutationrate/ if $hasdynmut;
    print DAT '# ', join(' ', @keys),"\n";
    foreach ( @gens ) {
        foreach my $key ( @keys ) {
            print DAT "$_->{$key} ";
        }
        print DAT "\n";
    }
    close DAT;

    open G, '>', $gpfn or die "Cannot open output file $gpfn: $!";
    print G <<END;
set term png size 1200,480
set output "$outfn"
set xrange [0:*]
#set yrange [-45:$ytop]
set y2range [0:.1]
set y2tics 0,.05,.1
set grid y2tics
set xlabel "Generation (Took $sectot sec, $secgen sec/gen)"
set ylabel "Fitness (0 is best)"
set y2label "Mutation rate, 0-1"
set style data lines
set title "$fn"
END
    print G "plot '$datfn' using 1:2 title 'Average Fitness' \\\n";
    print G "   , '$datfn' using 1:3 title 'Best Fitness' \\\n";
    if ( $hasdynmut ) {
        print G "   , '$datfn' using 1:4 title 'Leading Value' \\\n";
        print G "   , '$datfn' using 1:5 title 'Trailing Value' \\\n";
        print G "   , '$datfn' using 1:6 title 'Mutation Rate' axes x1y2 lt -1 \\\n";
    }
    print G "\n";
    close G;

    system 'gnuplot', $gpfn;
}
