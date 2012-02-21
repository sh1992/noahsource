#!/usr/bin/perl
#
# extract-individuals.pl - Extract individuals from a ga-spectroscopy log file.
#

use FindBin;
use lib $FindBin::Bin;
use gaspec;
use warnings;
use strict;

my $kind = shift @ARGV;
my $templatebasename = shift @ARGV;
if ( defined($kind) ) {
    $kind = uc $kind;
    if ( $kind eq 'ITEM' ) { $kind = 'FITM|ITEM' }
    elsif ( $kind ne 'BEST' and $kind ne 'FITM' ) { $kind = '' }
}
my @template = ();
my @suffixes = ('.int', '.var');
my $GA_segment_size = 32;
if ( $templatebasename ) {
    for my $suffix ( @suffixes ) {
        my $fn = $templatebasename.$suffix;
        open F, '<', $fn or die "Can't open $fn: $!";
        push @template, '';
        while (<F>) { $template[$#template] .= $_ }
        close F;
    }
}

die "Usage: $0 <BEST|FITM|ITEM> <templatebasename> <files>\n"
    unless $kind and $templatebasename and @ARGV;

foreach my $fn ( @ARGV ) {
    *F = open_compressed($fn);
READLOOP:
    while (<F>) {
        next unless $_;
        if ( m/^CFG.\s+linkbc/ ) { die "linkbc not currently supported" }
        if ( m/^(?:$kind)\s+(\d+)\s+(\d+)/ ) {
            my ($gen, $item) = ($1, $2);
            my @params = ();
            while ($_ && m/GD\s+(\d+)\s+(\d+)/) {
                # Format and save this parameter
                my ($idx, $val) = ($1, $2);
                if ( $idx > 2 ) { # DJ, DJK, DK, dj, dk
                    my $zero = GA_djk_zero();
                    $val = (($val > $zero) ? '-' : '').
                           (($val>$zero) ? ($val-$zero) : ($zero-$val));
                    $val .= 'E-12';
                }
                else { $val .= 'E-05' }
                push @params, $val;
                # Read the next line
                $_ = <F>;
                last if $_ and m/^\S/;
            }
            if ( @params ) {
                # Write the files for this individual
                my $outbasename = convert_filename($fn, "-$gen-$item");
                for ( my $i = 0; $i < @suffixes; $i++ ) {
                    my $buf = $template[$i];
                    $buf =~ s/%g(\d+)%/$params[$1]/g;
                    $buf =~ s/%e\d+%/0/g;
                    my $outfn = $outbasename.$suffixes[$i];
                    open OUT, '>', $outfn or die "Can't write to $outfn: $!";
                    print OUT $buf;
                    close OUT;
                }
                # Don't read another line if we went around the inner loop
                # because we haven't processed the current line yet.
                redo READLOOP;
            }
        }
    }
}
# Separate function because the GtkSourceView syntax highlighter doesn't seem
# to support the << left shift operator, only the <<HERE operation.
sub GA_djk_zero { return ~(1<<($GA_segment_size-1)) }
