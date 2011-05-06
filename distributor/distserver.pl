#!/usr/bin/perl
#
# distserver.pl - Distributed Computing Server
#
use threads;
use IO::Socket::INET;
use IO::Select;
use Digest::MD5;
use JSON;
use warnings;
use strict;

# Configuration
my $VERSION = 20110428;
my $PORT = 9933;
my $deadline = 35;

# Create listener socket
my $listener = IO::Socket::INET->new
    (LocalAddr => '0.0.0.0', LocalPort => $PORT, Listen => 5, Reuse => 1)
    or die "Can't listen on $PORT: $!";
my $select = IO::Select->new($listener);
local $SIG{'PIPE'} = 'IGNORE';

# Start problem-specific portion
#our $JOBSENDERTOKEN = Digest::MD5::md5_hex('DISTJOBS', $VERSION, $$, rand());
my $JOBSENDERTOKEN = Digest::MD5::md5_hex('DISTJOBS', 'gaspecdist', 'aptronym');
print "TOKEN: $JOBSENDERTOKEN\n";

my %workunits = ();
my %workers = ();
my %clients = ();
my %filenos = ();           # Convert fileno to socket ID
my $nextid = 1;

print "distserver ready\n";
while ( 1 ) {
    foreach my $sock (  my @ready = $select->can_read(5) ) {
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
                $select->remove($sock);
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
                    my @k = keys %workers;
                    my $worker = undef;
                    # Pick a random worker (FIXME: Pick fastest worker?)
                    my $i = int(rand()*@k);
                    for ( my $c = 0; $c < @k; $i = ($i+1)%@k ) {
                        $c++;
                        my $w = $workers{$k[$i]};
                        if ( $w->{assigned} < $w->{threads} ) {
                            $worker = $w;
                            last;
                        }
                    }
                    if ( $worker ) {
                        my $str = to_json($worker);
                        print $sock "WORKER $str\n";
                    }
                    else { print $sock "NOWORKERS\n" }
                }
                elsif ( $l =~ m/^DISPATCH (.+)$/ ) {
                    my $json = $1;
                    my $obj = eval { from_json($json) } || undef;
                    $obj = {} if ref($obj) ne 'HASH';
                    my $workerid = $obj->{worker};
                    my $workersock = $clients{$workers{$workerid}{sock}}{sock};
                    my $err = '';
                    # Workunit sanity checks
                    if ( !$obj->{id} ) { $err .= ' No workunit ID.' }
                    # FIXME: Check validity of ID (gaspec-something)
                    elsif ( !$workerid ) { $err .= ' No worker.' }
                    elsif ( !exists($workers{$workerid}) )
                        { $err .= ' Invalid worker.' }
                    elsif ( $workers{$workerid}{assigned} >=
                            $workers{$workerid}{threads} )
                        { $err .= ' Worker is busy.' }
                    elsif ( !$workersock ) {
                        DeleteWorker($workerid);
                        $err .= ' Worker is offline.'
                    }
                    if ( $err ) {
                        my $json = to_json({id => $obj->{id}||undef,
                                            error => $err});
                        print $sock "WORKREJECTED $json\n";
                        next;
                    }
                    $workunits{$obj->{id}} =
                        { obj => $obj, source => $id, worker => $workerid,
                          heartbeat => time, queried => 0, pestered => 0 };
                    print $sock "WORKACCEPTED\n";
                    $workers{$workerid}{assigned}++;
                    print $workersock "WORK $json\n";
                }
                else { print $sock "ERR Invalid command\n" }
            }
            elsif ( $clients{$id}{kind} == 1 ) {    # Worker
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
                        print $sock "ERR $error\n";
                        my $w = $workers{$clients{$id}{worker}};
                        $w->{assigned}--
                            unless $w->{assigned} <= 0;
                    }
                    else {
                        DeleteWorkunit($obj->{id}, $state, $json);
                    }
                }
                elsif ( $l =~ m/^WORKING (.+)$/ ) {
                    my ($json) = $1;
                    my $obj = eval { from_json($json) } || undef;
                    $obj = [] if ref($obj) ne 'ARRAY';
                    my $now = time;
                    foreach ( @$obj ) {
                        next unless exists($workunits{$_});
                        next unless $workunits{$_}{worker} eq $clients{$id}{worker};
                        $workunits{$_}{heartbeat} = $now;
                        $workunits{$_}{queried} = 0;
                    }
                    my $w = $workers{$clients{$id}{worker}};
                    my $n = scalar(@$obj);
                    if ( $w->{assigned} != $n ) {
                        print "Worker taskcount update $w->{assigned}->$n\n";
                        NewWorker() if $n < $w->{assigned};
                        $w->{assigned} = $n;
                    }
                    foreach ( keys %workunits ) {
                        next unless $workunits{$_}{worker} eq $clients{$id}{worker};
                        if ( $workunits{$_}{heartbeat} < $now ) {
                            my $json = to_json
                                ({id => $_, error => 'Client forgot about me'});
                            DeleteWorkunit($_, 'FAILED', $json);
                        }
                    }
                }
                elsif ( $l =~ m/^THREADS (\d+)$/ ) {
                    my $t = $1;
                    my $w = $workers{$clients{$id}{worker}};
                    NewWorker() if $t > $w->{threads};
                    $w->{threads} = $t;
                }
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
                        $clients{$id}{kind} = 2;
                        print $sock "OK You can now submit work\n";
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
                              threads => 0, assigned => 0 };
                        # FIXME: Client should detect number of threads.
                        # FIXME: Check already-assigned jobs.
                        print $sock "OK I will now send you work\n";
                        print $sock "QUERYWORK\n";
                        NewWorker();
                    }
                }
                else {
                    print $sock "ERR Login first\n";
                }
            }
        }
    }
    # Probe workunits in case they are gone
    my $now = time;
    my @k = keys %workunits;
    foreach my $k ( @k ) {
        next unless $workunits{$k}{heartbeat}+$deadline < $now;
        if ( ( $workunits{$k}{queried} >= $workunits{$k}{heartbeat} and
               $workunits{$k}{queried}+$deadline < $now ) or
              ( $workunits{$k}{pestered} > 3 ) ) {
            # Cancel workunit
            print "Failing $k [$workunits{$k}{pestered}]\n";
            if ( exists($workers{$workunits{$k}{worker}}) ) {
                DeleteWorker($workunits{$k}{worker});
            }
            my $err = {id => $k, error => 'Timed out waiting for worker'};
            DeleteWorkunit($k, 'FAILED', to_json($err));
        }
        elsif ( $workunits{$k}{queried} < $workunits{$k}{heartbeat} ) {
            print "Probing $k\n";
            # FIXME: What if client is broken and never finishes?
            $workunits{$k}{queried} = $now;
            $workunits{$k}{pestered}++;
            next unless exists($workers{$workunits{$k}{worker}});
            my $w = $workers{$workunits{$k}{worker}};
            next unless $w && exists($w->{sock}) && $w->{sock};
            next unless exists($clients{$w->{sock}});
            my $sock = $clients{$w->{sock}}{sock};
            next unless $sock;
            print $sock "QUERYWORK\n";
            # Do query
        }
    }
}

sub DeleteWorkunit {
    my ($id, $status, $json) = @_;
    my $dispatcher = $workunits{$id}{source};
    my $dispatchsock = $clients{$dispatcher}{sock};
    print $dispatchsock "WORK$status $json\n" if $dispatchsock;
    if ( exists($workers{$workunits{$id}{worker}}) ) {
        my $w = $workers{$workunits{$id}{worker}};
        $w->{assigned}-- unless $w->{assigned} <= 0;
    }
    delete $workunits{$id};
}

sub DeleteWorker {
    my ($id) = @_;
    print "Deleting worker $id\n";
    return unless exists($workers{$id});
    my $sockid = $workers{$id}{sock};
    if ( $clients{$sockid}{sock} ) {
        $select->remove($clients{$sockid}{sock});
        close $clients{$sockid}{sock};
    }
    delete $clients{$sockid};
    delete $workers{$id};
}

sub NewWorker {
    foreach ( values %clients ) {
        warn "Corrupt client list"
            unless ref($_) eq 'HASH' and defined($_->{kind});
        my $c = $_->{sock};
        print $c "NEWWORKER\n" if $_->{kind} == 2;
    }
}
