#
# gaspec.pm - Helper functions for handling log files, filenames, etc.
#

package gaspec;
use base 'Exporter';
use warnings;
use strict;

our $SEGMENTS = 8; # Must match ga-spectroscopy.c

our @EXPORT = qw(convert_filename open_compressed parse_logfile);

my %compressors = (bz2 => 'bzip2', xz => 'xz', gz => 'gzip', lzma => 'lzma',
                   Z => 'compress');
my $compressre = join '|', keys %compressors;

sub convert_filename {
    my ($in, $outsuffix) = @_;
    $in =~ s/\.($compressre)$//g;
    $in =~ s/\.([a-z0-9]{0,5})$//;
    $in =~ s/-(fitness|params)$//;
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

sub parse_logfile {
    my $logfn = shift @_;
    my %callbacks = @_;
    my $fh = open_compressed($logfn) or return undef;
    my $F = '[-+.0-9Ee]+';  # Floating-point number
READLOOP:
    while (<$fh>) {
        next unless $_;
        if ( m/^CFG.\s+linkbc/ ) { die "linkbc not currently supported" }
        elsif ( ( $callbacks{item} || ( m/^BEST/ and $callbacks{best} ) ) &&
                  m/^(ITEM|FITM|BEST)\s+(\d+)\s+(\d+)\s+.+\s+score\s+($F)\s+orig\s+($F)/ ) {
            my ($kind, $gen, $item, $score, $fitness) = ($1, $2, $3, $4, $5);
            my @params = ();
            while ($_ && m/GD\s+(\d+)\s+(\d+)/) {
                # Format and save this parameter
                my ($ridx, $idx, $val) = ($1, $1%$SEGMENTS, $2);
                if ( $idx > 2 ) { # DJ, DJK, DK, dj, dk
                    my $zero = GA_djk_zero();
                    $val = (($val > $zero) ? '-' : '').
                           (($val>$zero) ? ($val-$zero) : ($zero-$val));
                    $val .= 'E-12';
                }
                else { $val .= 'E-05' }
                push @params, $val;
                # Read the next line
                $_ = <$fh>;
                last if $_ and m/^\S/; # Not indented => Not part of this item
            }
            if ( @params ) {
                my $func = $kind eq 'BEST' ? $callbacks{best} : $callbacks{item};
                $func->(kind => $kind, generation => $gen, individual => $item,
                        params => \@params,
                        score => $score, fitness => $fitness)
                    if $func;
                # Don't read another line if we went around the inner loop
                # because we haven't processed the current line yet.
                redo READLOOP;
            }
        }
        elsif ( $callbacks{dynm} &&
                ( m/DYNM\s+(\d+)\s+(?:AVG\s+($F))?(?:\s+A\s+($F)\s+B\s+($F)\s+D\s+($F)\s+R\s+($F))?/ ||
                  m/AVRG (\d+)\s+($F)/ ) ) { # Old style
            my ($gen,$mean,$a,$b,$d,$r) = ($1,$2,$3,$4,$5,$6);
            #$hasdynmut = 1 if $x || defined($a);
            $callbacks{dynm}->(generation => $gen, avg => $mean,
                               leading => $a, trailing => $b,
                               difference => $d, mutationrate => $r);
        }
        elsif ( $callbacks{time} &&
                m/Took ($F) seconds \(($F) sec\/gen\)/ ) {
            $callbacks{time}->(total => $1, genavg => $2);
        }
    }
    close $fh;
    return 1;
}

# Separate function because the GtkSourceView syntax highlighter doesn't seem
# to support the << left shift operator, only the <<HERE operation.
my $GA_segment_size = 32;
sub GA_djk_zero { return ~(1<<($GA_segment_size-1)) }

1;
