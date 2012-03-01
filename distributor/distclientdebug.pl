#!/usr/bin/perl
#
# distclientdebug.pl - Debugging output for ga-spectroscopy distributor client.
#

use threads;
use threads::shared;
use FindBin;
BEGIN { chdir $FindBin::Bin; push @INC, "$FindBin::Bin/lib" }
use warnings;
use strict;

sub catch_zap { print "Exiting\n"; OnExit(); exit(1) }
$SIG{INT} = \&catch_zap;

our $THREADCOUNT;
if ( ! do 'distclient.pl' ) {
    print "$@\n$!\n";
    exit 1;
}

$|=1;
my @events :shared = ();

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
    my $status = undef;
    { # Wait for work
        my $got_zap = 0; local $SIG{INT} = sub { $got_zap++ };
        lock(@events);
        cond_timedwait(@events, time()+2) until @events or $got_zap;
        $status = shift @events unless $got_zap;
    }
    last unless $status;
    last if $status->{mode} eq 'STOPPING';
    #use JSON;
    #print to_json($status),"\n";
    my $str = "t".($status->{thread}||0)." is $status->{mode}";
    $str .= sprintf(" (%03d)",$status->{progress}) if exists($status->{progress});
    print length($str).":'$str'\n";
}

OnExit();
exit 0;

