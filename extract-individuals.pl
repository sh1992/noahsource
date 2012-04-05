#!/usr/bin/perl
#
# extract-individuals.pl - Extract individuals from a ga-spectroscopy log file.
#

use FindBin;
use lib $FindBin::Bin;
use gaspec;
use warnings;
use strict;

# Which individuals should be extracted
my $kind = shift @ARGV;
if ( defined($kind) ) {
    $kind = lc $kind;
    $kind = undef if $kind ne 'item' and $kind ne 'best';
}

# Load the template
my $templatebasename = shift @ARGV;
my @template = ();
my @suffixes = ('.int', '.var');
my $GA_segment_size = 32;
if ( $templatebasename ) {
    for my $suffix ( @suffixes ) {
        my $fn = $templatebasename.$suffix;
        open F, '<', $fn or die "Can't open template file $fn: $!";
        push @template, '';
        while (<F>) { $template[$#template] .= $_ }
        close F;
    }
}

die "Usage: $0 <BEST|ITEM> <templatebasename> <files>\n"
    unless $kind and $templatebasename and @ARGV;

foreach my $fn ( @ARGV ) {
    my $logfn = convert_filename($fn, '.log.');
    sub got_item {
        my %params = @_;
        my $outbasename = convert_filename($logfn,
            "-$params{generation}-$params{individual}");
        for ( my $i = 0; $i < @suffixes; $i++ ) {
            my $buf = $template[$i];
            $buf =~ s/%g(\d+)%/$params{params}[$1]/g;
            $buf =~ s/%e\d+%/0/g;
            my $outfn = $outbasename.$suffixes[$i];
            open OUT, '>', $outfn or die "Can't write to $outfn: $!";
            print OUT $buf;
            close OUT;
        }
    }
    parse_logfile($logfn, $kind => \&got_item)
        or die "Can't parse logfile $fn ($!?)";
}
