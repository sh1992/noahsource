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

our $statusposter = sub {
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
        lock(@events);
        cond_timedwait(@events, time()+2) until @events;
        $status = shift @events;
    }
    last unless $status;
    use JSON;
    print to_json($status),"\n";
}

OnExit();
exit 0;

