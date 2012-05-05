#!/usr/bin/perl
#
# plot-params.pl - Plot the parameter values as a function of generation.
#

use FindBin;
use lib $FindBin::Bin;
use Getopt::Long;
use gaspec;
use warnings;
use strict;

my $format = 'png';
my $output = undef;
my $mytitle = undef;
my $exactfile = undef;
my $usepercent = 1;
my $xlabel = 1;
my $yrange = '*:*';
my $size = '';
my $trim = '';
my $xrange = '0:*';
(GetOptions('format=s' => \$format, 'output=s' => \$output,
            'title=s' => \$mytitle, 'exact=s' => \$exactfile,
            'percent!' => \$usepercent, 'yrange=s' => \$yrange,
            'xrange=s' => \$xrange, 'xlabel!' => \$xlabel, 'size=s' => \$size,
            'trim=s' => \$trim) and @ARGV)
    or die <<EOF;
Usage: plot-params.pl [--format={png|latex}] [--output=<basename>]
          [--title=<title>] [--exact=<file>] [--[no-]percent]
          [--yrange=<range>] [--[no-]xlabel] [--size=<size>]
          [--trim=<tripspec>] <file> [...]
EOF

my ($term, $outsuffix);
if ( $format eq 'png' ) {
    $size ||= '1200,480';
    $term = "png size $size";
    $outsuffix = '.png';
}
elsif ( $format eq 'latex' or $format eq 'latexcolor' ) {
    $size ||= '6in,3.99in';
    $term = "epslatex size $size dl 2";
    $term .= " color solid" if $format =~ s/color$//;
    $outsuffix = '.tex';
}
else { die "Undefined format: $format\n" }

if ( $trim =~ m/^\s*([-+.0-9eE]+)\s*[-:]\s*([-+.0-9eE]+)\s*$/ )
    { $trim = [$1, $2] }
elsif ( $trim ) { die "Invalid trim specification: $trim" }

foreach my $fn ( @ARGV ) {
    my $logfn = convert_filename($fn,'.log.');
    my $outfn = convert_filename($output||$fn,'-params'.$outsuffix);
    my $datfn = convert_filename($fn,'-params.dat');
    my $gpfn = convert_filename($output||$fn,'-params.gnuplot');
    print "$logfn\n";

    my @values = ();
    # Parse the logfile
    my $got_item = sub {
        my %params = @_;
        # Get an array slice of the parameter values
        push @values, [$params{generation}, @{$params{params}}];
    };
    if ( parse_logfile($logfn, best => $got_item) ) {
        $datfn = convert_filename($output||$fn,'-params.dat');
        open DAT, '>', $datfn or die "Cannot open output file $datfn: $!";
        foreach ( @values ) {
            print DAT join(' ', @$_), "\n";
        }
        close DAT;
    }
    elsif ( open F, '<', $datfn ) {
        print "\n\nWARNING: Using preexisting parameter data file $datfn\n\n\n";
        while (my $l = <F> ) {
            push @values, [split ' ', $l];
        }
        close F;
    }
    else {
        die "\n\nERROR: No parameter data or log file for $fn: $!\n\n\n";
    }

    my @names = qw/A B C/;
    # Exact values for comparison
    my @exact = ();
    if ( $exactfile ) {
        open F, '<', $exactfile or die "Can't open $exactfile: $!";
        while (<F>) {
            next unless m/^\s*([-+0-9.Ee]+)\s*$/;
            push @exact, $1;
        }
        close F;
        die sprintf 'Exact value file contained %d items, need at least %d',
            scalar(@exact), scalar(@names)
            unless @exact >= @names;
    }
    else { @exact = @{$values[-1]}[1..@names] }

    my $title = defined($mytitle) ? $mytitle : $fn;

    open G, '>', $gpfn or die "Cannot open output file $gpfn: $!";
    print G <<END;
set term $term
set output "$outfn"

set yrange [$yrange] writeback
set xrange [$xrange]                # If trim is on, xmax will be set later
set style data lines
unset key                       # Enabled later
END
    print G<<END if $term =~ m/color/;
set style line 2 lc 3
set style line 5 lc -1
END
    print G <<END if $format eq 'latex';
set lmargin at screen .14
set rmargin at screen .96       # If trim is on, also set for other part
END
    my $multiploton = 0;
    if ( $title ) {
        if ( $format eq 'latex' ) {
            my $where = $term =~ m/color/ ? 'graph' : 'screen'; # Hack
            print G "set label 1 \"$title\" at graph .5, $where .89 center\n"
        }
        elsif ( $trim ) { # Set title on entire canvas if trim is set
            print G "set multiplot title \"$title\"\nset tmargin 2\n";
            $multiploton = 1;
        }
        else { print G "set title \"$title\"\n" }
    }
    if ( $trim && !$multiploton ) { print G "set multiplot\n" }
    print G <<END if $trim;
set size 0.75, 1
set origin 0,0
set rmargin 0                   # Trim is on, override right margin
set xrange [:$trim->[0]]
END
    if ( $usepercent ) {
        print G "set ylabel \"Percent deviation\"\n"
            if $exactfile;
        print G "set ylabel \"Percent deviation (from final value)\"\n"
            if !$exactfile;
    }
    else { print G "set ylabel \"Value (MHz)\"\n" }
    print G "set xlabel \"Generation\"\n" if $xlabel;
    my $plotcmdcount = $trim ? 2 : 1;
    while ( $plotcmdcount-- > 0 ) {
        print G "set key\n" if $plotcmdcount == 0;
        print G 'plot';
        my @lts = qw/1 2 5/;
        for ( my $i = 0; $i < @names; $i++ ) {
            my $lt = $lts[$i%@lts];
            my $percent = $usepercent ? ('/'.$exact[$i].'*100-100') : '';
            print G '   ,' if $i > 0;
            print G " '$datfn' using 1:(\$". ($i+2) . $percent .
                    ") ls $lt title '$names[$i]' \\\n";
        }
        if ( $usepercent ) { print G "   , 0 lt 0 title '' \\\n" }
        else {
            foreach ( @{$values[$#values]}[1..3] ) {
                print G "   , $_ lt 0 title '' \\\n";
            }
        }
        print G "\n";
        if ( $trim && $plotcmdcount ) {
            my $interval = $values[-1][0]-$trim->[1];
            if ( $interval > 0 ) { $interval /= 2 }
            else { $interval = 'autofreq' }
            print G <<EOF;

# Set up other part of multiplot
set size 0.2, 1
set origin 0.80, 0
set lmargin 0
set rmargin at screen .96
set ytics autofreq format ""
unset ylabel
set xrange [$trim->[1]:*]
set xtics $interval
set yrange restore
EOF
            print G "unset label 1\n" if $title and $format eq 'latex';
        }
    }
    print G "unset multiplot\n" if $trim;
    close G;

    system('gnuplot', $gpfn) == 0 or die "gnuplot failed";
    (my $epsfn = $outfn) =~ s/\.tex$/.eps/;
    system('epstopdf', $epsfn) == 0 or die "epstopdf failed"
        if $format eq 'latex'; # Support pdfLaTeX
}
