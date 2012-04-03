#!/usr/bin/perl
#
# plotfitness.pl - Plot a graph of fitness as a function of generation.
# Takes ga-spectroscopy log files as input.
#

use FindBin;
use lib $FindBin::Bin;
use Getopt::Long;
use File::Basename;
use gaspec;
use warnings;
use strict;

my $format = 'png';
my $output = undef;
my $mytitle = undef;
(GetOptions('format=s' => \$format, 'output=s' => \$output,
            'title=s' => \$mytitle) and @ARGV)
    or die <<EOF;
Usage: $0 [--format={png|latex}] [--output=<basename>] [--title=<title>]
          <file> [...]\n"
EOF

my ($term, $outsuffix);
if ( $format eq 'png' ) { $term = 'png size 1200,480'; $outsuffix = '.png' }
elsif ( $format eq 'latex' ) { $term = 'epslatex size 6in,3.99in dl 2'; $outsuffix = '.tex' }
else { die "Undefined format: $format\n" }

foreach my $fn ( @ARGV ) {
    my $hasdynmut = 0;
    my ($sectot, $secgen) = (0, 0);
    my $ytop = 0;
    my $logfn = convert_filename($fn,'.log.');
    my $f = open_compressed($logfn);
    my $outfn = convert_filename($output||$fn,'-fitness'.$outsuffix);
    my $datfn = convert_filename($f ? ($output||$fn) : $fn,'-fitness.dat');
    my $gpfn = convert_filename($output||$fn,'-fitness.gnuplot');
    if ( $f ) {
        my @gens = ();
        while (<$f>) {
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
        close $f;
        $ytop++;

        # Write fitness data file
        open DAT, '>', $datfn or die "Cannot open output file $datfn: $!";
        my @keys = qw/gen avg best/;
        push @keys, qw/leading trailing mutationrate/ if $hasdynmut;
        print DAT '# ', join(' ', @keys),"\n";
        print DAT "# Took $sectot sec, $secgen sec/gen\n";
        foreach ( @gens ) {
            foreach my $key ( @keys ) {
                print DAT "$_->{$key} ";
            }
            print DAT "\n";
        }
        close DAT;
    }
    elsif ( open F, '<', $datfn ) {
        print "\n\nWARNING: Using preexisting fitness data file $datfn\n\n\n";
        my $l = <F>;
        $hasdynmut = 1 if $l =~ m/mutationrate/;
        $l = <F>;
        ($sectot,$secgen) = ($1,$2) if $l =~ m/Took (\S+) sec, (\S+) sec.gen/;
        close F;
    }
    else {
        die "\n\nERROR: No fitness data or log file for $fn\n\n\n";
    }

    my $title = defined($mytitle) ? $mytitle : $fn;

    open G, '>', $gpfn or die "Cannot open output file $gpfn: $!";
    print G <<END;
set term $term
set output "$outfn"

#set yrange [-45:$ytop]
set ylabel "Fitness (0 is best)"
set logscale y
set yrange [:] reverse
set format y "-%g"

set xrange [0:*]
set style data lines
set key left Left reverse
set lmargin at screen .14
set rmargin at screen .96
END
    print G "set title \"$title\"\n" if $title;
    print G 'set xlabel "Generation',
        ($sectot&&$secgen)?" (Took $sectot sec, $secgen sec/gen)":'',"\"\n";
    print G <<END if $hasdynmut;
set rmargin at screen .9
set y2range [0:.1]
set y2tics 0,.05,.1
set grid y2tics
set y2label "Mutation rate, 0-1"
END
    my ($lt1,$lt2) = $format eq 'latex' ? (2,1) : (1,2);
    print G "plot '$datfn' using 1:(-\$3) lt $lt2 title 'Fitness of Best Individual' \\\n";
    print G "   , '$datfn' using 1:(-\$2) lt $lt1 title 'Average Fitness of Population' \\\n";
    if ( $hasdynmut ) {
        print G "   , '$datfn' using 1:(-\$4) lt 3 title 'Leading Value' \\\n";
        print G "   , '$datfn' using 1:(-\$5) lt 4 title 'Trailing Value' \\\n";
        print G "   , '$datfn' using 1:6      lt -1 title 'Mutation Rate' axes x1y2 \\\n";
    }
    print G "\n";
    close G;

    system 'gnuplot', $gpfn;
    (my $epsfn = $outfn) =~ s/\.tex$/.eps/;
    system('epstopdf', $epsfn) if $format eq 'latex'; # Support pdfLaTeX
}
