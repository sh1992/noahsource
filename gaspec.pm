#
# gaspec.pm - Helper functions for handling log files, filenames, etc.
#

package gaspec;
use base 'Exporter';
use warnings;
use strict;

our @EXPORT = qw(convert_filename open_compressed);

my %compressors = (bz2 => 'bzip2', xz => 'xz', gz => 'gzip', lzma => 'lzma',
                   Z => 'compress');
my $compressre = join '|', keys %compressors;

sub convert_filename {
    my ($in, $outsuffix) = @_;
    $in =~ s/\.($compressre)$//g;
    $in =~ s/\.([a-z0-9]{0,5})$//;
    if ( !$outsuffix ) { return $in }
    if ( $outsuffix eq '.log.' ) {
        $outsuffix = '.log';
        if ( !-f $in.$outsuffix ) {
            foreach ( keys %compressors ) {
                my $f = "$in$outsuffix.$_";
                if ( -e $f ) { $outsuffix .= ".$_"; last }
            }
        }
    }
    return $in.$outsuffix;
}

sub open_compressed {
    my ($in) = @_;
    my $mode = '<';
    my @open = ();
    if ( $in =~ m/\.([a-zZ0-9]{0,5})$/ and exists($compressors{$1}) )
        { $mode = '-|'; @open = ($compressors{$1}, '-dc') }

    my $fh = undef;
    open($fh, $mode, @open, $in) or return undef;
    return $fh;
}

1;
