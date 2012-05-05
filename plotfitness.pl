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
my $xlabel = 1;
my $yrange = '*:*';
my $size = '';
(GetOptions('format=s' => \$format, 'output=s' => \$output,
            'title=s' => \$mytitle, 'xlabel!' => \$xlabel,
            'yrange=s' => \$yrange,
            'size=s' => \$size) and @ARGV)
    or die <<EOF;
Usage: plotfitness.pl [--format={png|latex}] [--output=<basename>]
        [--yrange=<range>] [--title=<title>] [--[no-]xlabel] [--size=<size>]
        <file> [...]
EOF

my ($term, $outsuffix);
if ( $format eq 'png' ) {
    $size ||= '1200,480';
    $term = "png size $size";
    $outsuffix = '.png';
}
elsif ( $format eq 'latex' ) {
    $size ||= '6in,3.99in';
    $term = "epslatex size $size dl 2";
    $outsuffix = '.tex';
}
else { die "Undefined format: $format\n" }

foreach my $fn ( @ARGV ) {
    my $logfn = convert_filename($fn,'.log.');
    my $outfn = convert_filename($output||$fn,'-fitness'.$outsuffix);
    my $datfn = convert_filename($fn,'-fitness.dat');
    my $gpfn = convert_filename($output||$fn,'-fitness.gnuplot');
    print "$logfn\n";

    my $hasdynmut = 0;
    my ($sectot, $secgen) = (0, 0);
    my $ytop = 0;
    # Callback handlers for parse_logfile
    my @gens = ();
    my $got_dynm = sub {
        my %params = @_;
        $hasdynmut = 1 if defined($params{leading});
        $gens[$params{generation}]{avg} = $params{avg}
            if defined($params{avg});
        $gens[$params{generation}]{$_} = $params{$_}
            foreach qw/leading trailing difference mutationrate/;
    };
    my $got_best = sub {
        my %params = @_;
        $gens[$params{generation}]{gen} = $params{generation};
        $gens[$params{generation}]{best} = $params{fitness};
        $ytop = int($params{fitness});
    };
    my $got_time = sub {
        my %params = @_;
        $sectot = $params{total}; $secgen = $params{genavg};
    };
    if ( parse_logfile($logfn, dynm => $got_dynm, best => $got_best,
                               time => $got_time) ) {
        $datfn = convert_filename($output||$fn,'-fitness.dat');
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
        die "\n\nERROR: No fitness data or log file for $fn: $!\n\n\n";
    }

    my $title = defined($mytitle) ? $mytitle : $fn;

    open G, '>', $gpfn or die "Cannot open output file $gpfn: $!";
    print G <<END;
set term $term
set output "$outfn"

#set yrange [-45:$ytop]
set ylabel "Fitness (arbitrary units)"
set logscale y
set yrange [$yrange] reverse
set format y "-%g"

set xrange [0:*]
set style data lines
set key left Left reverse
END
    print G <<END if $format eq 'latex';
set lmargin at screen .14
set rmargin at screen .96
END
    if ( $title ) {
        print G "set title \"$title\"\n" if $format ne 'latex';
        print G "set label \"$title\" at screen .945, screen .89 right\n"
            if $format eq 'latex';
    }
    print G 'set xlabel "Generation',
        ($sectot&&$secgen)?" (Took $sectot sec, $secgen sec/gen)":'',"\"\n"
        if $xlabel;
    if ( $hasdynmut ) {
        print G "set rmargin at screen .9\n" if $format eq 'latex';
        print G <<END;
set y2range [0:.1]
set y2tics 0,.05,.1
set grid y2tics
set y2label "Mutation rate, 0-1"
END
    }
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

    system('gnuplot', $gpfn) == 0 or die "gnuplot failed";
    (my $epsfn = $outfn) =~ s/\.tex$/.eps/;
    system('epstopdf', $epsfn) == 0 or die "epstopdf failed"
        if $format eq 'latex'; # Support pdfLaTeX
}
