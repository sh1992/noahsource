#!/usr/bin/perl
#
# distclientcli.pl - Console interface for ga-spectroscopy distributor client.
#

# # Support unthreaded Perl via the forks module. WARNING: Very slow.
# use Config;
# use if !$Config{usethreads} forks;
use threads;
use threads::shared;
use FindBin;
BEGIN { chdir $FindBin::Bin; push @INC, "$FindBin::Bin/lib" }
use JSON;
use warnings;
use strict;

if ( !eval('use Win32::Console::ANSI; 1') and $^O eq 'MSWin32' ) {
    die "On Win32, Win32::Console::ANSI is required\n";
}

our $THREADCOUNT;

sub catch_zap {
    select(STDOUT);
    print "\nExiting\n";
    OnExit() and exit(1);
}
$SIG{INT} = \&catch_zap;

if ( ! do 'distclient.pl' ) {
    print "$@\n$!\n";
    exit 1;
}


package StatusbarFH;
# StatusbarFH: Wrapper for STDOUT that keeps a statusbar of n lines.
use warnings;
use strict;

my $lock :shared = 0;

# ANSI abstraction
our $ERASE_LINE = "\e[2K";
sub MOVE_UP { return "\e[$_[0]A" }
sub MOVE_DOWN { return "\e[$_[0]B" }
sub INSERT_LINES { return "\e[$_[0]L" }

sub TIEHANDLE {
    my ($class, $lines) = @_;
    return bless {lines => $lines||1}, $class;
}
sub PRINT {
    my $self = shift;
    my $str = join('', @_);
    $str .= "\n" unless $str =~ m/\n$/;
    my $lines = 0;
    $lines += int(length($_)/80)+(length($_)%80?1:0) foreach split "\n", $str;
    lock($lock);
    print STDOUT "\n"x$lines, MOVE_UP($lines+$self->{lines}),
                 INSERT_LINES($lines), $str, "\r", $ERASE_LINE,
                 MOVE_DOWN($self->{lines});
}
sub PRINTF { my $self = shift; return $self->PRINT(sprintf @_) }
#sub WRITE  { my $self = shift; return syswrite STDOUT, @_ }
package main;

$|=1;
tie *STATUSBARFH, 'StatusbarFH', $THREADCOUNT+2; select STATUSBARFH;

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

print STDOUT "\n";
my @lastmsg = ();
for ( my $i = 0; $i <= $THREADCOUNT; $i++ ) {
    print STDOUT $lastmsg[$i] = "[$i] Idle", "\n";
}

while ( 1 ) {
    my $status = undef;
    { # Wait for work
        # If we accept the signal here, @events will still be locked.
        my $got_zap = 0; local $SIG{INT} = sub { $got_zap++ };
        lock(@events);
        cond_timedwait(@events, time()+2) until @events or $got_zap;
        $status = shift @events unless $got_zap;
    }
    last unless $status;
    my $thread = $status->{thread} || 0;
    my $mode = $status->{mode} || '';

    if ( $mode eq 'STARTING' ) { # FIXME
        print to_json($status), "\n";
    }
    elsif ( $mode eq 'STOPPING' ) { last }

    my $newstr = "[$thread] ".RenderStatus($status);
    if ( $thread && $status->{progress} ) {
        $newstr = sprintf("%-74.74s %3d%%", $newstr,
                          $status->{progress}*100/$status->{range});
    }

    my $linedelta = $THREADCOUNT+1-$thread;
    print STDOUT StatusbarFH::MOVE_UP($linedelta), $StatusbarFH::ERASE_LINE,
                 $newstr, "\r", StatusbarFH::MOVE_DOWN($linedelta);
    $lastmsg[$thread] = $newstr;
}
select(STDOUT);
print "\nFinished\n";
OnExit();
exit 0;

