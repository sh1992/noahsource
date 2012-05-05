#!/usr/bin/perl
#
# distserver.pl - Distributed Computing Server
#
use threads;
use IO::Socket::INET;
use IO::Select;
use Time::HiRes;
use Digest::MD5;
use JSON;
use warnings;
use strict;

# Configuration
my $VERSION = 20120210;
my $PORT = 9933;

# Minimum client version; older clients will be jailed. They will not be sent
# any work, and clients version >=20120210 will display an error message.
my $MINCLIENT = 20120209;

# Deadline factors
my ($LAZYDEADLINE, $AGRESSIVEDEADLINE) = (5, 1.5);
# How long do we wait for a distributor to provide new work after being told
# of an available worker?
my $DISTTIMEOUT = 3;
# How frequently to output the list of workers
my $DUMPINTERVAL = 60;
# How frequently to check for completed workunits
my $PROBEINTERVAL = 1;

# Create listener socket
my $listener = IO::Socket::INET->new
    (LocalAddr => '0.0.0.0', LocalPort => $PORT, Listen => 5, Reuse => 1)
    or die "Can't listen on $PORT: $!";
my $select = IO::Select->new($listener);
local $SIG{'PIPE'} = 'IGNORE';

# Start problem-specific portion
my $JOBSENDERTOKEN = Digest::MD5::md5_hex('DISTJOBS', $VERSION, $$, rand());
print "TOKEN: $JOBSENDERTOKEN\n";
open F, '>', 'distserver.key' or die "Cannot load distserver.key: $!";
print F $JOBSENDERTOKEN,"\n";
close F;

my %workunits = ();
my @workunits = ();         # List of keys, sorted by lazy deadline
my %workers = ();
my ($totalthreads, $busythreads) = (0, 0);
my ($totaldistributors, $idledistributors) = (0, 0);
my @monitors = ();          # Clients monitoring traffic through server
my %clients = ();
my %filenos = ();           # Convert fileno to socket ID
my $nextid = 1;
# When we last saved a copy of the worker list
my $lastworkerdump = time-int($DUMPINTERVAL/4);
my $freeworkersince = 0;    # How long there's been an available thread
my $lastbroadcast = 0;
my $lastagressive = 0;      # Last time an agressive cancelation was performed
my $goaway_message = 'Distributed Computing Client upgrade required.';

print "distserver ready\n";
while ( 1 ) {
    foreach my $sock (  my @ready = $select->can_read($PROBEINTERVAL) ) {
        if ( $sock eq $listener ) {
            # Accept new client
            my $client = $sock->accept();
            my $id = $nextid; $nextid++;
            $filenos{fileno($client)} = $id;
            $clients{$id} = { id => $id, sock => $client, buf => '', kind => 0 };
            $select->add($client);
            print $client "HELLO $VERSION\n";
            next;
        }
        my $id = $filenos{fileno($sock)};
        my $rc = sysread($sock, $clients{$id}{buf}, 512,
                         length($clients{$id}{buf}));
        if ( !$rc ) {
            warn "sysread from client ID=$id failed: $!" if !defined($rc);
            # If $rc is defined, then we have EOF
            if ( $clients{$id}{kind} == 1 ) {
                DeleteWorker($clients{$id}{worker});
            }
            else {
                if ( $clients{$id}{kind} == 2 ) {
                    # Let other distributors use this distributor's workers.
                    NewWorker() if $freeworkersince;
                    $totaldistributors--;
                    $idledistributors-- if $clients{$id}{idle};
                }
                elsif ( $clients{$id}{kind} == 3 ) {
                    # Remove monitor from list.
                    for ( my $i = 0; $i < @monitors; $i++ ) {
                        next unless $monitors[$i] eq $sock;
                        splice @monitors, $i, 1;
                        last;
                    }
                }
                $select->remove($sock);
                close($sock);
                delete $clients{$id};
            }
            # Cancel jobs attached to worker
            next;
        }
        while ( $clients{$id}{buf} =~ s/^(.*?)\r*\n// ) {
            my $l = $1;
            { (my $pl = $l) =~ s/data:[^"]+/data:.../g; print "D$id: $pl\n" }
            if ( $clients{$id}{kind} == 2 ) {       # Job Generator
                if ( $l =~ m/^HAVEWORK/ ) {
                    $idledistributors-- if $clients{$id}{idle};
                    $clients{$id}{idle} = 0;
                    if ( !$freeworkersince && $busythreads >= $totalthreads ) {
                        print $sock "NOWORKERS\n";
                        next;
                    }
                    my @k = keys %workers;
                    my $worker = undef;
                    # Pick a random worker (FIXME: Pick fastest worker?)
                    my $i = int(rand()*@k);
                    for ( my $c = 0; $c < @k; $c++ ) {
                        $i = ($i+1)%@k;
                        my $w = $workers{$k[$i]};
                        if ( ($w->{assigned}||0) < ($w->{threads}||0) ) {
                            $worker = $w;
                            last;
                        }
                    }
                    if ( $worker ) {
                        my $str = to_json($worker);
                        print $sock "WORKER $str\n";
                    }
                    else { print $sock "NOWORKERS\n" } # Should never happen
                    # Somebody is using workers, so reset the timeout.
                    $freeworkersince = time if $busythreads < $totalthreads;
                }
                elsif ( $l =~ m/^NOMOREWORK/ ) {
                    $idledistributors++ unless $clients{$id}{idle};
                    $clients{$id}{idle} = 1;
                }
                elsif ( $l =~ m/^DISPATCH (.+)$/ ) {
                    my $json = $1;
                    my $obj = eval { from_json($json) } || undef;
                    $obj = {} if ref($obj) ne 'HASH';
                    my $workerid = $obj->{worker};
                    my $workersock = $workers{$workerid}{sock} ?
                        $clients{$workers{$workerid}{sock}}{sock} : 0;
                    my $err = '';
                    # Workunit sanity checks
                    if ( !$obj->{id} ) { $err .= ' No workunit ID.' }
                    elsif ( !$obj->{duration} || $obj->{duration} <= 0 )
                        { $err .= ' No duration.' }
                    # FIXME: Check validity of ID (gaspec-something)
                    elsif ( !$workerid ) { $err .= ' No worker.' }
                    elsif ( !exists($workers{$workerid}) )
                        { $err .= ' Invalid worker.' }
                    elsif ( ($workers{$workerid}{assigned}||0) >=
                            ($workers{$workerid}{threads}||0) )
                        { $err .= ' Worker is busy.' }
                    elsif ( !$workersock ) {
                        DeleteWorker($workerid);
                        $err .= ' Worker is offline.'
                    }
                    if ( $err ) {
                        $json = to_json({id => $obj->{id}||undef,
                                         error => $err});
                        print $sock "WORKREJECTED $json\n";
                        next;
                    }
                    # Send the work to the client.
                    print $sock "WORKACCEPTED\n";
                    print $workersock "WORK $json\n";
                    $workers{$workerid}{assigned}++;
                    $busythreads++;
                    $freeworkersince = 0 if $busythreads >= $totalthreads;
                    # Save the workunit in the hash
                    my $now = Time::HiRes::time;
                    $workunits{$obj->{id}} =
                        { obj => $obj, source => $id, worker => $workerid,
                          starttime => $now };
                    # Insert workunit into @workunits
                    my $deadline = $now + $LAZYDEADLINE*$obj->{duration};
                    for ( my $i = 0; $i <= @workunits; $i++ ) {
                        if ( $i == @workunits ) {
                            push @workunits, $obj->{id};
                            last;
                        }
                        my $tw = $workunits{$workunits[$i]};
                        my $twd = $tw->{starttime} +
                            $LAZYDEADLINE*$tw->{obj}{duration};
                        if ( $deadline < $twd ) {
                            splice @workunits, $i, 0, $obj->{id};
                            last;
                        }
                    }
                    NotifyMonitors('CREATE', 'WORKUNIT', $obj->{id});
                    # Monitor does not need UPDATE WORKER message also.
                }
                else { print $sock "ERR Invalid command\n" }
            }
            elsif ( $clients{$id}{kind} == 1 ) {    # Worker
                my $w = $workers{$clients{$id}{worker}};
                # Track our last-seen-time for the worker
                $w->{seen} = time;
                if ( $l =~ m/^WORKACCEPTED/ ) {}
                elsif ( $l =~ m/^WORK(FINISHED|FAILED) (.+)$/ ) {
                    my ($state,$json) = ($1, $2);
                    my $obj = eval { from_json($json) } || undef;
                    my $error = '';
                    if ( ref($obj) ne 'HASH' ) { $error = 'Invalid JSON' }
                    elsif ( !$obj->{id} or !exists($workunits{$obj->{id}}) )
                        { $error = 'Workunit does not exist' }
                    elsif ( $workunits{$obj->{id}}{worker} ne $clients{$id}{worker} )
                        { $error = 'Workunit not assigned to you' }
                    if ( $error ) {
                        print $sock "ERR $error\nQUERYWORK\n";
                    }
                    else {
                        DeleteWorkunit($obj->{id}, $state, $json);
                        # Update last-successfully-completed-workunit time
                        $w->{seenwork} = time if $state eq 'FINISHED';
                    }
                }
                elsif ( $l =~ m/^WORKING (.+)$/ ) {
                    my ($json) = $1;
                    my $obj = eval { from_json($json) } || undef;
                    $obj = [] if ref($obj) ne 'ARRAY';
                    my %found = ();
                    foreach ( @$obj ) {
                        next unless exists($workunits{$_});
                        next unless $workunits{$_}{worker} eq $clients{$id}{worker};
                        # This is a workunit we have assigned to this worker.
                        $found{$_} = 1;
                    }
                    # Is the worker operating on an unexpected number of
                    # workunits?
                    my $n = scalar(@$obj);
                    if ( $w->{assigned} != $n ) {
                        print "Worker taskcount update $w->{assigned}\->$n\n";
                        $busythreads += $n-$w->{assigned};
                        $busythreads = 0 if $busythreads < 0;
                        if ( $busythreads >= $totalthreads )
                            { $freeworkersince = 0 }
                        elsif ( !$freeworkersince ) { $freeworkersince = time }
                        NewWorker() if $n < $w->{assigned};
                        $w->{assigned} = $n;
                        NotifyMonitors('UPDATE', 'WORKER', $w->{id});
                    }
                    # Cancel all the workunits the client "forgot" about.
                    foreach ( keys %workunits ) {
                        next unless $workunits{$_}{worker} eq $clients{$id}{worker};
                        next if exists($found{$_});
                        my $json = to_json
                            ({id => $_, error => 'Client forgot about me'});
                        DeleteWorkunit($_, 'FAILED', $json);
                    }
                }
                elsif ( $l =~ m/^THREADS (\d+)$/ ) {
                    if ( !$w->{jailed} ) {
                        # Do not allow jailed clients to change number of
                        # threads. This will keep us from sending them work.
                        my $t = $1;
                        $totalthreads += $t-$w->{threads};
                        $totalthreads = 0 if $totalthreads < 0;
                        if ( $t > $w->{threads} ) {
                            $freeworkersince = time unless $freeworkersince;
                            NewWorker();
                        }
                        else { # Reducing number of threads.
                            $freeworkersince = 0
                                if $busythreads >= $totalthreads;
                        }
                        $w->{threads} = $t;
                        NotifyMonitors('UPDATE', 'WORKER', $w->{id});
                    }
                }
                elsif ( $l =~ m/^PING/ ) { print $sock "PONG\n" }
                elsif ( $l =~ m/^PONG/ ) { }
                else { print $sock "ERR Invalid command\n" }
            }
            else {
                # Process message from client
                if ( $l =~ m/^HELLO (\S+) (\S+) ([0-9a-fA-F]+)$/i ) {
                    my ($sockver, $name, $ident) = ($1, $2, lc $3);
                    $name =~ tr/-_.0-9a-zA-Z//cd;
                    # Hello messages should have Version and ID/Permissions.
                    if ( $ident eq $JOBSENDERTOKEN ) {
                        # FIXME: Check name for collisions
                        print $sock "OK You can now submit work\n";
                        $clients{$id}{kind} = 2;
                        $clients{$id}{idle} = 1;
                        $totaldistributors++;
                        $idledistributors++;
                    }
                    elsif ( $sockver eq '0' ) {
                        print $sock "ERR Invalid job sender token\n";
                    }
                    else {
                        if ( exists($workers{$ident}) ) {
                            DeleteWorker($ident);
                        }
                        $clients{$id}{kind} = 1;
                        $clients{$id}{worker} = $ident;
                        $workers{$ident} =
                            { sock => $id, id => $ident, name => $name,
                              threads => 0, assigned => 0, seen => time,
                              seenwork => 0, jailed => 0, ver => $sockver };
                        if ( $sockver < $MINCLIENT ) {
                            print $sock "GOAWAY $goaway_message\n";
                            print "Client $name has version $sockver; too old!\n";
                            $workers{$ident}{jailed} = 1;
                        }
                        else {
                            # Worker client.
                            print $sock "OK I will now send you work\n";
                            print $sock "GRACEFACTOR $AGRESSIVEDEADLINE\n";
                            # Is the client working on any jobs right now?
                            print $sock "QUERYWORK\n";
                            NewWorker();
                        }
                        NotifyMonitors('CREATE', 'WORKER', $ident);
                    }
                }
                elsif ( $l =~ m/^MONITOR/ ) {
                    push @monitors, $sock;
                    $clients{$id}{kind} = 3;
                    foreach ( values %workers ) {
                        next unless exists($_->{name}) and defined($_->{name});
                        next unless exists($_->{sock}) and defined($_->{sock});
                        print $sock "UPDATE WORKER $_->{id} ",to_json($_),"\n";
                    }
                    foreach ( values %workunits ) {
                        next unless exists($_->{obj}) and defined($_->{obj});
                        next unless exists($_->{worker}) and defined($_->{worker});
                        print $sock "UPDATE WORKUNIT $_->{obj}{id} ",
                            to_json(ExportWorkunit($_->{obj}{id})),"\n";
                    }
                }
                else {
                    print $sock "ERR Login first\n";
                }
            }
        }
    }
    # Probe workunits in case they are gone
    my $hrnow = Time::HiRes::time;
    my $now = time;
    # If there are free workers and we've told everybody, but none has
    # provided work, try to make work by agressively canceling overdue
    # workunits. FIXME: Allow distributor to speed this up by reporting
    # "no work available."
    my $agressive = $freeworkersince && $now > $lastagressive+$DISTTIMEOUT &&
        ( $totaldistributors <= $idledistributors ||
          $now > $freeworkersince+$DISTTIMEOUT*2 );
    my $deadlinefactor = $agressive ? $AGRESSIVEDEADLINE : $LAZYDEADLINE;
    my $i = 0;
    while ( my $k = $workunits[$i] ) {
        my $deadline = $workunits{$k}{obj}{duration}*$deadlinefactor;
        if ( $workunits{$k}{starttime}+$deadline > $hrnow )
            { ($i++,next) if $agressive; last }
        # Workunit is running too slowly. Cancel the workunit.
        print "Failing $k, ", $hrnow - $workunits{$k}{starttime} -
                $workunits{$k}{obj}{duration}, " seconds late [$agressive].\n";
        if ( exists($workers{$workunits{$k}{worker}}) ) {
            DeleteWorker($workunits{$k}{worker});
        }
        my $err = {id => $k, error => 'Timed out waiting for worker'};
        DeleteWorkunit($k, 'FAILED', to_json($err));
        # FIXME: Send ABORTWORK message to worker
        # To maximize use of workers, only agressively cancel one workunit
        # at once, this will allow the unused capacity to be taken up by redos
        # of one workunit at a time.
        if ( $agressive ) { $lastagressive = $now; last }
    }
    # Dump worker list every minute
    DumpWorkers() if $now >= $lastworkerdump + $DUMPINTERVAL;
    # If there's been an idle worker for a while and we've only told one
    # dispatcher, tell the rest in case they have work.
    NewWorker() if $freeworkersince and $freeworkersince != $lastbroadcast and
        $now >= $freeworkersince+$DISTTIMEOUT;
}

sub DeleteWorkunit {
    my ($id, $status, $json) = @_;
    my $dispatcher = $workunits{$id}{source};
    my $dispatchsock = exists($clients{$dispatcher}) ?
        $clients{$dispatcher}{sock} : undef;
    if ( $dispatchsock ) {
        print $dispatchsock "WORK$status $json\n";
    }
    my $wid = $workunits{$id}{worker};
    if ( exists($workers{$wid}) ) {
        my $w = $workers{$wid};
        $w->{assigned}-- unless $w->{assigned} <= 0;
        $busythreads-- unless $busythreads <= 0;
        # If this dispatcher doesn't have more work, this worker will remain
        # idle until we broadcast its existance to all other dispatchers.
        $freeworkersince = time
            if !$freeworkersince && $busythreads < $totalthreads;
        # This dispatcher disappeared, alert all other dispatchers immediately.
        NewWorker() if !$dispatchsock;
        #NotifyMonitors('UPDATE', 'WORKER', $wid); # Client can figure out
    }
    # Remove workunit from lists
    for ( my $i = 0; $i < @workunits; $i++ ) {
        next unless $workunits[$i] eq $id;
        splice @workunits, $i, 1;
        last;
    }
    NotifyMonitors('DELETE', 'WORKUNIT', $id, $status);
    delete $workunits{$id};
}

sub DeleteWorker {
    my ($id) = @_;
    print "Deleting worker $id\n";
    NotifyMonitors('DELETE', 'WORKER', $id);
    return unless exists($workers{$id});
    my $sockid = $workers{$id}{sock};
    if ( $clients{$sockid}{sock} ) {
        $select->remove($clients{$sockid}{sock});
        close $clients{$sockid}{sock};
    }
    $totalthreads -= $workers{$id}{threads};
    $totalthreads = 0 if $totalthreads < 0;
    $busythreads -= $workers{$id}{assigned};
    $busythreads = 0 if $busythreads < 0;
    $freeworkersince = 0 if $busythreads >= $totalthreads;
    delete $clients{$sockid};
    delete $workers{$id};
}

# Broadcast the existance of a free worker
sub NewWorker {
    return if !$freeworkersince || $lastbroadcast == $freeworkersince;
    foreach ( values %clients ) {
        warn "Corrupt client list"
            unless ref($_) eq 'HASH' and defined($_->{kind});
        my $c = $_->{sock};
        next unless $_->{kind} == 2 && !$_->{idle};
        print $c "NEWWORKER\n"
    }
    $lastbroadcast = $freeworkersince;
}

# Save a dump of all connected workers (for monitoring)
sub DumpWorkers {
    unless ( open WF, '>', 'workers.dat' ) {
        warn "Can't write worker dump: $!";
        return;
    }
    unless ( open WJ, '>', 'workers.json' ) {
        close WF;
        warn "Can't write worker JSON dump: $!";
        return;
    }
    print WJ '{';
    my $comma = 0;
    my $now = time;
    foreach my $id ( keys %workers ) {
        next unless exists($workers{$id}{name}) and defined($workers{$id}{name});
        next unless exists($workers{$id}{sock}) and defined($workers{$id}{sock});
        print WF "$id\t$workers{$id}{name}\t$workers{$id}{seen}\t",
                 "$workers{$id}{seenwork}\t$workers{$id}{threads}\t",
                 "$workers{$id}{ver}\n";
        if ( $comma ) { print WJ ',' } else { $comma = 1 }
        print WJ "\n    \"$id\": ",to_json($workers{$id});
        if ( $workers{$id}{seen}+300 < $now ) {
            my $sock = $clients{$workers{$id}{sock}}{sock};
            print $sock "PING\n";
        }
    }
    print WJ "\n}\n";
    close WF;
    close WJ;
    $lastworkerdump = $now;
}

sub NotifyMonitors {
    return unless @monitors;
    my ($message, $itemtype, $id, $arg) = @_;
    my $prestr = "$message $itemtype $id";
    my $str = '';
    #print $prestr,"\n";
    if ( $message ne 'DELETE' ) {
        if ( $itemtype eq 'WORKER' ) { $str .= ' '.to_json($workers{$id}) }
        elsif ( $itemtype eq 'WORKUNIT' )
            { $str .= ' '.to_json(ExportWorkunit($id)) }
    }
    elsif ( $arg ) { $str = $arg };
    $str = ' '.$str if $str;
    foreach ( @monitors ) {
        print $_ $prestr, $str, "\n";
    }
}
sub ExportWorkunit {
    my ($id) = @_;
    return {'worker' => $workunits{$id}{worker},
            'starttime' => $workunits{$id}{starttime},
            'duration' => $workunits{$id}{obj}{duration},
            'nitems' => $workunits{$id}{obj}{nitems}};
}
