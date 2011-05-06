#!/usr/bin/perl
#
# distclientcli.pl - Console interface for ga-spectroscopy distributor client.
#
use threads;
use threads::shared;
use FindBin;
BEGIN { chdir $FindBin::Bin; push @INC, "$FindBin::Bin/lib" }
use warnings;
use strict;

if ( !eval('use Win32::Console::ANSI; 1') and $^O eq 'MSWin32' ) {
    die "On Win32, Win32::Console::ANSI is required\n";
}

sub catch_zap { print "Exiting\n"; OnExit(); exit(1) }
$SIG{INT} = \&catch_zap;

our $THREADCOUNT;
if ( ! do 'distclient.pl' ) {
    print "$@\n$!\n";
    exit 1;
}

$|=1;
my @lastmsg = ();
for ( my $i = 0; $i <= $THREADCOUNT; $i++ ) {
    print $lastmsg[$i] = "[$i] Idle", "\n";
}
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
    my $thread = $status->{thread} || 0;
    my $mode = $status->{mode} || '';

    if ( $mode eq 'STARTING' ) { # FIXME
        ansi_up($THREADCOUNT+1);
        use JSON;
        print_padded(to_json($status),"\n",length($lastmsg[0]));
        my $len = 79;
        foreach ( @lastmsg ) {
            print_padded($_,"\n",$len);
            $len = length($_);
        }
        #print join("\n",@lastmsg);
    }

    my $newstr = "[$thread] ".RenderStatus($status);
    if ( $thread && $status->{progress} ) {
        $newstr = sprintf("%-74.74s %3d%%",$newstr, $status->{progress}*100/$status->{range});
    }

    my $linedelta = $THREADCOUNT+1-$thread;
    ansi_up($linedelta);
    print_padded($newstr,"\r", length($lastmsg[$thread]));
    $lastmsg[$thread] = $newstr;
    ansi_down($linedelta);
}

OnExit();
exit 0;

sub ansi_up { print "\x1b[$_[0]A" }
sub ansi_down { print "\x1b[$_[0]B" }
sub print_padded {
    my ($str, $suffix, $padsize) = @_;
    my $pad = $padsize-length($str);
    print $str,($pad>0)?(' 'x$pad):'',$suffix;
}
