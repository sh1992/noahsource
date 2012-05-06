#!/usr/bin/perl
#
# distclientplain.pl - Text interface for ga-spectroscopy distributor client.
#

use threads;
use threads::shared;
use FindBin;
BEGIN { chdir $FindBin::Bin; push @INC, "$FindBin::Bin/lib" }
use warnings;
use strict;

my $DEBUG = @ARGV && $ARGV[0] =~ m/debug|all/i;

sub catch_zap { print "Exiting\n"; OnExit(); exit(1) }
$SIG{INT} = \&catch_zap;

our $THREADCOUNT;
if ( ! do 'distclient.pl' ) {
    print "$@\n$!\n";
    exit 1;
}

$|=1;
my @events :shared = (); my @last = ();
our %callbacks;
$callbacks{poststatus} = sub {
    my ($result) = @_;
    lock(@events);
    push @events, $result;
    #print to_json($result),"\n";
    cond_signal(@events);
};
StartClient();
while ( 1 ) {
    my $st = undef;
    { # Wait for work
        my $got_zap = 0; local $SIG{INT} = sub { $got_zap++ };
        lock(@events);
        cond_timedwait(@events, time()+2) until @events or $got_zap;
        $st = shift @events unless $got_zap;
    }
    last if !$st or ($st->{mode} eq 'STOPPING');
    my ($thr,$mode,$now) = ($st->{thread}||0, $st->{mode}, time);
    my $str = sprintf("[%2d] %11s %s", $thr, $mode, RenderStatus($st));
    $str .= sprintf(" (%04d/%04d)",$st->{progress},$st->{range})
        if $st->{progress} && $st->{range} && $st->{progress} < $st->{range};
    if ( $DEBUG ) { print length($str).":'$str'\n" }
    elsif ( $mode ne 'WORKING' or $last[$thr]+2 < $now )
        { print "$str\n"; $last[$thr] = $mode eq 'WORKING' ? $now : 0 }
}

OnExit();
exit 0;

