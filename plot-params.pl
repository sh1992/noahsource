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
    my $logfn = convert_filename($fn,'.log.');
    my $outfn = convert_filename($output||$fn,'-params'.$outsuffix);
    my $datfn = convert_filename($fn,'-params.dat');
    my $gpfn = convert_filename($output||$fn,'-params.gnuplot');

    my @values = ();
    # Parse the logfile
    sub got_item {
        my %params = @_;
        # Get an array slice of the parameter values
        push @values, [$params{generation}, @{$params{params}}];
    }
    if ( parse_logfile($fn, best => \&got_item) ) {
        $datfn = convert_filename($output||$fn,'-params.dat');
        open DAT, '>', $datfn or die "Cannot open output file $datfn: $!";
        foreach ( @values ) {
            print DAT join(' ', @$_), "\n";
        }
        close DAT;
    }
    elsif ( !-f $datfn ) {
        die "\n\nERROR: No parameter data or log file for $fn\n\n\n";
    }

    my $title = defined($mytitle) ? $mytitle : $fn;
    my $usepercent = 1;

    open G, '>', $gpfn or die "Cannot open output file $gpfn: $!";
    print G <<END;
set term $term
set output "$outfn"

set xrange [0:*]
set style data lines
#set key left Left reverse
END
    print G <<END if $format eq 'latex';
set lmargin at screen .14
set rmargin at screen .96
END
    print G "set ylabel \"Value (MHz)\"\n" if !$usepercent;
    print G "set ylabel \"Percent deviation from final value\"\n" if $usepercent;
    print G "set title \"$title\"\n" if $title;
    print G "set xlabel \"Generation\"\n";
    my @names = qw/A B C/;
    print G 'plot';
    for ( my $i = 0; $i < @names; $i++ ) {
        my $percent = $usepercent ? ('/'.$values[$#values][$i+1].'*100-100') : '';
        print G '   ,' if $i > 0;
        print G " '$datfn' using 1:(\$". ($i+2) . $percent .
                ") title '$names[$i]' \\\n";
    }
    if ( $usepercent ) { print G "   , 0 lt 0 title '' \\\n" }
    else {
        foreach ( @{$values[$#values]}[1..3] ) {
            print G "   , $_ lt 0 title '' \\\n";
        }
    }
    print G "\n";
    close G;

    system 'gnuplot', $gpfn;
    (my $epsfn = $outfn) =~ s/\.tex$/.eps/;
    system('epstopdf', $epsfn) if $format eq 'latex'; # Support pdfLaTeX
}
