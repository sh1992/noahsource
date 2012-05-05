#!/usr/bin/perl
#
# gaspecdist.pl - Distributed Computing Dispatcher for ga-spectroscopy
#
# Receives generations from ga-spectroscopy processes, packages them into
# workunits, and sends workunits to the Server.
#
use IO::Socket::INET;
use IO::Select;
use Digest::MD5;
use File::Spec;
use File::Temp;
use JSON;
use Time::HiRes;
use File::Copy;
use URI;
use warnings;
use strict;

my ($DHOST, $DPORT) = ('localhost', 9933);  # Connect to server
my ($LHOST, $LPORT) = ('localhost', 2222);  # Listen for ga-spectroscopy
my ($WHOST, $WPORT) = ('localhost', 9990);  # HTTP host and port

# Load some server information from server.conf
open F, '<', 'server.conf' or die "Cannot load server.conf: $!";
my $serverconf = from_json(join '', <F>);
close F;
$WHOST = $serverconf->{host} if $serverconf->{host}; # HTTP hostname
$DPORT = $serverconf->{port} if $serverconf->{port}; # Server port

my $SPECDIR = '..';                         # CWD for ga-spectroscopy processes
my $DATADIR = '../data';                    # Location of data directory
my $DATAURL = "http://:$WPORT/spec/data";   # URL of the same (default $WHOST)
my $TEMPDIR = '../temp';                    # Location of a temporary location
my $TEMPURL = "http://:$WPORT/spec/temp";   # URL of the same (default $WHOST)
my $APPDIR = '.';
my $APPNAME = 'ga-spectroscopy.app';
my $APPURL = "$TEMPURL/$APPNAME";
my $INBOXDIR = '../inbox';
#my $UPLOADURL = "http://$WHOST:$WPORT/spec/upload"; # URL of uploader
my $UPLOADURL = 'data:';
# Name of data and temporary directories on workers
my ($REMOTEDATADIR,$REMOTETEMPDIR) = ('data','temp');
my $WUDURATION = 10;
my $WORKERSPEED_AVERAGES = 10;

# Load key for distserver. It is currently randomly generated each time the
# distserver starts up.
open F, '<', 'distserver.key' or die "Cannot load distserver.key: $!";
my $JOBSENDERTOKEN = <F>;
$JOBSENDERTOKEN =~ s/[\r\n]+$//;
close F;
die "No job-sender token" unless $JOBSENDERTOKEN;
my $DISPATCHID = "gaspec-$$";

my $distsock = IO::Socket::INET->new(PeerAddr => $DHOST, PeerPort => $DPORT)
    or die "Cannot connect to $DHOST:$DPORT: $!";

my $galisten = IO::Socket::INET->new(LocalAddr => $LHOST, LocalPort => $LPORT,
                                     Listen => 5, Reuse => 1)
    or die "Cannot listen on $LHOST:$LPORT: $!";
local $SIG{'PIPE'} = 'IGNORE';

my $select = IO::Select->new($distsock, $galisten);
my $distbuf = '';

# my %workers = ();
my %workunits = ();
my %socks = ();             # Sockets (and GA processes)
my @items = ();             # GA Individuals
my @freeitems = ();         # List of free indexes for @items
my $askingforworkers = 0;
my $uniqclientid = 0;
my $haveworkcache = 0;      # No work available
my $starttime = time;

#threads->create(\&mythread);
mythread();
sub mythread {
    print "gaspecdist ready\n";
    while ( my @ready = $select->can_read() ) {
        foreach my $sock ( @ready ) {
            if ( $sock eq $distsock ) {
                my $rc = sysread($sock, $distbuf, 512, length($distbuf));
                die "sysread from distsock failed: $!" unless defined($rc);
                die "sysread from distsock: EOF" unless $rc;
                # Handle data from socket
                while ( $distbuf =~ s/^(.*?)\r*\n// ) {
                    my $l = $1;
                    { (my $pl = $l) =~ s/data:[^"]+/data:.../g; print "S0: $pl\n" }
                    if ( $l =~ m/^HELLO/ ) {
                        print $sock "HELLO 0 $DISPATCHID $JOBSENDERTOKEN\n";
                    }
                    elsif ( $l =~ m/^NEWWORKER/ ) { TrySending() }
                    elsif ( $l =~ m/^NOWORKERS/ ) { $askingforworkers = 0 }
                    elsif ( $l =~ m/^WORKER (.+)/ ) {
                        my $data = $1;
                        $askingforworkers = 0;
                        my $worker = from_json($data); # FIXME errorcheck
                        SendWork($worker);
                        TrySending();
                    }
                    elsif ( $l =~ m/^ERR (.+)/ ) {
                        my $msg = $1;
                        die "Invalid token" if $msg =~ m/Invalid.*token/;
                    }
                    elsif ( $l =~ m/^WORK(FINISHED|FAILED|REJECTED) (.+)/ ) {
                        my ($command,$json) = ($1,$2);
                        my $reply = from_json($json);
                        WorkReturned($sock, $command, $reply);
                    }
                }
            }
            elsif ( $sock eq $galisten ) {
                my $client = $sock->accept();
                my $id = fileno($client);
                $uniqclientid++;
                $socks{$id} = { id => $id, sock => $client, buf => '',
                                uniqid => $uniqclientid,
                                config => '', configfile => undef,
                                remaining => 0, gentime => 0,
                                lastgentime => 0, genstart => Time::HiRes::time,
                                files => [[get_app()]], workerspeed => {} };
                $select->add($client);
            }
            else {
                my $id = fileno($sock);
                unless ( exists($socks{$id}) ) {
                    # Try not to crash
                    warn "Read from phantom socket $sock";
                    $select->remove($sock);
                    next;
                }
                my $rc = sysread($sock, $socks{$id}{buf}, 512,
                                 length($socks{$id}{buf}));
                if ( !$rc ) {
                    warn "sysread from sock ID=$id failed: $!" if !defined($rc);
                    # If $rc is defined, then we have EOF
                    $select->remove($sock);
                    # TODO: Cancel jobs, remove from @items, etc.
                    next;
                }
                # Handle data from socket
                HandleSocket($id);
            }
        }
    }
}

# Handle a returned workunit
sub WorkReturned {
    my ($sock, $command, $reply) = @_;
    # FIXME: Do some sanity checks
    my $source = -1;
    my $gasock = undef;
    my $receivedtime = Time::HiRes::time;
    my $fatal = 0;
    my $nreturned = 0;
    my $failed = $command eq 'FAILED';
    if ( $reply->{files} && @{$reply->{files}} ) {
        my ($valid, undef, $fn) = @{$reply->{files}[0]};
        my ($checksum, $buf);
        if ( $fn =~ m/^data:/ ) {
            my $u = URI->new($fn);
            $buf = $u->data;
        }
        else {
            $fn = "$INBOXDIR/$fn";
            $buf = '';
            if ( open F, '<', $fn ) {
                binmode F;
                $buf .= $_ while <F>;
                close F;
                unlink $fn;# unless $fatal;
            }
            else { warn "Open $fn failed: $!" }
        }
        $checksum = Digest::MD5::md5_hex($buf);
        if ( $checksum eq $valid ) {
            # Read the file
            while ($buf =~ m/([\s\S]*?\n)/g ) {
                my $l = $1;
                if ( $l =~ m/^ERROR/ ) {
                    print "ERROR LOG IN $fn\n";
                    $fatal = 1;
                    next;
                }
                elsif ( $fatal ) { print $l; next }
                next unless $l =~ m/^F (\d+) (\S+) E/;
                my ($item, $fitness) = ($1, $2);
                next unless defined($items[$item]);
                next unless $items[$item]{workunit} eq $reply->{id};
                $items[$item]{fitness} = $fitness;
                $items[$item]{received}++;
                # What job is this item from
                $source = $items[$item]{source};
                if ( !defined($source) || $source < 0 )
                    { warn "No gasock"; $source = -1 }
                # Return the item to that source
                $gasock = $socks{$source}{sock};
                printf $gasock "F %d %s\n",
                    $items[$item]{origindex},
                    $items[$item]{fitness};
                $socks{$source}{remaining}--;
                $nreturned++;
                $items[$item] = undef;
                push @freeitems, $item;
            }
        }
        else { warn "Checksum mismatch on $fn" }
    }
    else { warn "Error or no files on finished work" }
    if ( $fatal ) {
        # Get rid of this GA instance
        # FIXME
        warn "Fatal error on client";
    }
    # Look if we have more work to do or if we can report
    # the generation completed
    if ( $workunits{$reply->{id}}{nitems} != $nreturned ) {
        # Wrong number of items returned. Make sure no items are still
        # allocated to this workunit.
        warn "Wrong number of items returned\n";
        for ( my $i = 0; $i < @items; $i++ ) {
            my $item = $items[$i];
            next unless $item;
            # FIXME: Assumes numeric source IDs
            next if $source >= 0 && $item->{source} != $source;
            #$foundothers = 1;
            next unless $item->{workunit} eq $reply->{id};
            next unless $item->{sent} > $item->{received};
            $source = $item->{source};
            # Try sending this item again
            $nreturned = -1;
            $item->{sent} = 0; $item->{received} = 0;
            $item->{workunit} = ''; # FIXME item in multiple workunits?
            $haveworkcache = $i if $i < $haveworkcache;
        }
    }
    if ( $source >= 0 && $socks{$source}{remaining} <= 0  && !$failed ) {
        if ( $socks{$source}{remaining} != 0 )
            { warn "Source $source has $socks{$source}{remaining} remaining" }
        if ( !$gasock ) { warn "No gasock" }
        print $gasock "DONE\n";
        # Provisional generation duration.
        $socks{$source}{gentime} = Time::HiRes::time-$socks{$source}{genstart};
    }
    # Update workerspeed
    if ( $source >= 0 && ( $nreturned > 0 or $failed ) ) {
        my $wu = $workunits{$reply->{id}};
        my $duration = $receivedtime-$wu->{sent};   # Actual duration
        my $n = $wu->{nitems}/$duration;            # items/second
        $n = 1/60 if $n < 1/60 or                   # Minimum one item/minute
            $failed;                                # Failed workunit penalty
        # FIXME: max speed?
        # Average worker speeds to better accommodate EC2 micro instances
        my $ws = ($socks{$source}{workerspeed}{$wu->{worker}} ||= [5/$duration]);
        #push @$ws, $n;
        #shift @$ws if @$ws > $WORKERSPEED_AVERAGES;
        if ( $n < $ws->[0] ) { $ws->[0] = $n }  # Reduce speed immediately
        elsif ( $wu->{duration}-$duration > 2 ) { $ws->[0] *= 1.1 } # Fast
        elsif ( $duration-$wu->{duration} > 2 ) { $ws->[0] *= 0.9 } # Slow
        $ws->[0] = 1/60 if $ws->[0] < 1/60 or $n == 1/60;
        open F, '>>', '/tmp/workerspeed.log';
        print F "WORKERSPEED $wu->{worker} ",
            Time::HiRes::time-$starttime-$duration,' ',
            $duration," $wu->{duration} $wu->{nitems} $n\n";
        close F;
    }
    # Dispatch more work
    delete $workunits{$reply->{id}};
    TrySending();
}

sub get_app {
    my $appfile = "$APPDIR/$APPNAME";
    copy($appfile, "$TEMPDIR/$APPNAME")
        or warn "Can't copy $appfile to temp: $!";
    return (md5($appfile)||'', $APPURL, $APPNAME);
}

sub RemoveFileDependency {
    # Remove a file requirement for a given job
    my ($id, $fn) = @_;
    my $n = @{$socks{$id}{files}};
    for ( my $i = 0; $i < $n; $i++ ) {
        if ( $socks{$id}{files}[$i][2] eq "$REMOTETEMPDIR/$fn" ) {
            # Splice this element from the array and return
            splice @{$socks{$id}{files}}, $i, 1;
            return;
        }
    }
    warn "Could not remove $fn from dependencies of sock $id";
}

sub HandleSocket {
    my ($id) = @_;
    while ( $socks{$id}{buf} =~ s/^(.*?)\r*\n// ) {
        my $l = $1;
        print "G$id: $l\n" unless $l =~ m/^I /;
        if ( $l =~ m/^V .+$/ ) {
            my $configdata = $socks{$id}{config} . "$l\n";
            # Save config to file
            my $fh;
            if ( $socks{$id}{configfile} ) {
                # Remove the existing configfile from the list of dependencies
                my $fn = MakeRelPath($socks{$id}{configfile}, $TEMPDIR);
                RemoveFileDependency($id, $fn);
                # Open the existing configfile
                open $fh, '>', $socks{$id}{configfile}
                    or warn "Cannot write to $socks{$id}{configfile}";
            }
            else {
                # Create and open a new configfile
                ($fh, $socks{$id}{configfile}) =
                    File::Temp::tempfile("$DISPATCHID-config-XXXXX",
                                         DIR => $TEMPDIR, OPEN => 1,
                                         EXLOCK => 0, UNLINK => 0);
            }
            print $fh $configdata;
            close $fh;
            my $checksum = Digest::MD5::md5_hex($configdata);
            my $fn = MakeRelPath($socks{$id}{configfile}, $TEMPDIR);
            push @{$socks{$id}{files}},
                         [$checksum, "$TEMPURL/$fn", "$REMOTETEMPDIR/$fn"];
        }
        elsif ( $l =~ m/^G (\d+)/ ) {
            $socks{$id}{gen} = $1;
            $socks{$id}{wucount} = 'aaa';
            # Reset the generation timers here in order to support DR
            # which would have problems if these were to reset on DISPATCH.
            $socks{$id}{lastgentime} = $socks{$id}{gentime};
            $socks{$id}{genstart} = Time::HiRes::time;
            open F, '>>', '/tmp/workerspeed.log';
            print F "GENTIME ",$socks{$id}{gen}-1," $socks{$id}{lastgentime}\n";
            close F;
        }
        elsif ( $l =~ m/^I (\d+) (.+)$/ ) {
            my ($origindex, $values) = ($1, $2);
            $socks{$id}{remaining}++;
            AppendItem({source => $id, origindex => $origindex, workunit => '',
                        values => $values, fitness => undef});
        }
        elsif ( $l =~ m/^DISPATCH/ ) {
            # Prepare this generation to be sent
            my $founditems = 0;
            for ( my $i = 0; $i < @items; $i++ ) {
                my $item = $items[$i];
                if ( defined($item) and $item->{source} == $id ) {
                    $item->{sent} = 0;
                    $founditems = 1;
                    $haveworkcache = $i if $i < $haveworkcache;
                }
            }
            # Received empty population (entire population was in GA cache)
            if ( !$founditems ) {
                my $gasock = $socks{$id}{sock};
                print $gasock "DONE\n";
            }
            else {
                TrySending();
            }
        }
        elsif ( $l =~ m/^(CFG[A-Z0-9]) (\S+)(?: (.+))?/ ) {
            my ($type, $opt, $val) = ($1, $2, $3);
            my @suffixes = ();
            # Some configuration lines will specify dependency files.
            if ( $opt =~ m/^(match|template|drfile)$/ && defined($val) ) {
                # .INT+.VAR files
                @suffixes = ($opt eq 'template') ? ('.int','.var') : ('');
            }
            # Remove existing configuration items and dependency files.
            # Note: By default, perlre's $ will ignore newlines at the end of
            #       the string.
            if ( $socks{$id}{config} =~
                    s/(^|\n)CFG[A-Z0-9] $opt( ([^\n]*))?(\n|$)/$1/ &&
                 @suffixes && $3 ) {
                # Remove obsolete dependency files
                my (undef,undef,$oldfile) = File::Spec->splitpath($3);
                my $l = $socks{$id}{files};
                foreach my $suffix ( @suffixes ) {
                    my $thisfile = "$REMOTEDATADIR/$oldfile$suffix";
                    my $removed = 0;
                    for ( my $i = 0; $i < @$l; $i++ ) {
                        if ( $l->[$i][2] eq $thisfile )
                            { splice @$l, $i, 1; $removed++ }
                    }
                    warn "Remove $removed dependencies for $thisfile."
                        if $removed != 1;
                }
            }
            # If this configuration line specifies a file, locate the file
            # and add it to our list of dependencies.
            if ( @suffixes ) {
                # Determine our destination filename
                my (undef,undef,$outfile) = File::Spec->splitpath($val);
                $outfile = "$REMOTEDATADIR/$outfile";
                # Where is this data file really?
                foreach my $suffix ( @suffixes ) {
                    my $realfn = File::Spec->rel2abs($val.$suffix, $SPECDIR);
                    my $wwwfn = MakeRelPath($realfn, $DATADIR);
                    my $checksum = md5($realfn);
                    if ( !$checksum or !-e $realfn ) {
                        warn "Error checksumming $val=>$realfn or " .
                                "file does not exist (ID=$id config): $!";
                        next;
                    }
                    if ( $wwwfn =~ m/^\.\./ ) {
                        warn "Data file $val is not in $DATADIR";
                        next;
                    }
                    push @{$socks{$id}{files}},
                         [$checksum, "$DATAURL/$wwwfn", "$outfile$suffix"];
                }
                $val = $outfile;
            }
            $socks{$id}{config} .= "$type $opt" .
                                   (defined($val) ? " $val\n" : "\n");
        }
    }
}

sub MakeRelPath {
    my ($fn, $dir) = @_;
    $fn = File::Spec->rel2abs($fn)
        unless File::Spec->file_name_is_absolute($fn);
    return File::Spec->abs2rel($fn, $dir);
}

# Add an item to the global item list
sub AppendItem {
    my ($item) = @_;
    $item->{sent} = -1; $item->{received} = 0; # $item->{batch} = -1;
    while ( @freeitems ) {
        my $i = pop @freeitems;
        die "Freeitems list is broken" if $items[$i];
        $items[$i] = $item;
        return;
    }
    push @items, $item;
}

# Get the next individual we can send
sub GetNextIndividual {
    my ($client, $from) = @_;
    # Most common case: client=-1 (find any work from any GA). If this failed
    # last time, return a cached response until we get more work.
    my $i = $from+1;
    # The work cache keeps this function fast by tracking the starting point
    # and tracking the existance of available work.
    $i = $haveworkcache if $client < 0;
    for ( ; $i < @items; $i++ ) {
        if ( $items[$i] && $items[$i]{sent} == $items[$i]{received} &&
             $items[$i]{sent} == 0 ) {
            # This individual is ready to send.
            if ( $client >= 0 && $items[$i]{source} != $client ) {
                # But it's not from the right client. Keep going.
                $haveworkcache = $i if $i < $haveworkcache;
                next;
            }
            # Update the cache, and return this individual.
            # Leave the work cache pointer here in case we ignore the return
            # value (only checking if work exists and not collecting it yet).
            $haveworkcache = $i if $i < $haveworkcache;
            return $i;
         }
         # If we've passed the cache point and the individual was unsendable,
         # bring the cache point along until we find a sendable individual.
         $haveworkcache++ if $haveworkcache == $i;
    }
    return -1;
}

# Send work to a worker
sub SendWork {
    my ($worker) = @_;

    # Find an individual we can dispatch
    my $i = GetNextIndividual(-1, -1);
    return if $i < 0; # No work available
    my $client = $items[$i]->{source};

    # Workerspeed varies based on GA run settings
    my $ws = ($socks{$client}{workerspeed}{$worker->{id}} || []);
    #my $duration = $WUDURATION+int(5*rand());
    my $remaining = $socks{$client}{lastgentime} ?
        ( $socks{$client}{lastgentime} -
          ( Time::HiRes::time - $socks{$client}{genstart} ) ) : 15;
    my $duration = .5*$remaining;
    $duration = 2 if $duration < 2;
    $duration = $WUDURATION if $duration > $WUDURATION;
    my $n = 0;
    if ( @$ws >= 4 ) {
        # Determine the average speed of the worker.
        my $mean = 0;
        $mean += $_ foreach @$ws;
        $mean /= @$ws;
        my $denom = 0;
        foreach ( @$ws ) {
            # Weight excessively slow workunits more, excessively fast
            # workunits less.
            my $w = $_ > 1.1*$mean ? .707**(2*$_/$mean) :
                    ( $_ < .9*$mean ? 2**(2*$_/$mean) : 1);
            $n += $w*$_;
            $denom += $w;
        }
        $n = $n/$denom*$duration;
        $n = $n < 1 ? 1 : floor($n);
    }
    elsif ( @$ws == 1 ) { $n = $ws->[0]*$duration }
    else { $n = 10 } # Just send 10 workunits until we figure out the speed.

    # Generate workunit ID
    my $wuid = sprintf("%s-%04d-%04d%s", $DISPATCHID, $socks{$client}{uniqid},
                       $socks{$client}{gen}, $socks{$client}{wucount});
    $socks{$client}{wucount}++;

    # Generate our workunit population
    my $popfile = "G $socks{$client}{gen}\n";
    my $c = 0; #my @wuitems = ();
    do {
        last if $i < 0;
        $items[$i]{sent}++;
        $items[$i]{workunit} = $wuid; # FIXME: Item in multiple workunits?
        $c++; #push @wuitems, $i;
        $popfile .= "I $i $items[$i]{values}\n";
    } while ( $c < $n and ($i = GetNextIndividual($client, $i)) > -1 );

    my $checksum = Digest::MD5::md5_hex($popfile);
    my ($popurl, $popfn);
    if ( 0 ) {
        # Save population to file
        (my $fh, $popfn) =
            File::Temp::tempfile("$DISPATCHID-pop-XXXXX", DIR => $TEMPDIR,
                                 OPEN => 1, EXLOCK => 0, UNLINK => 0);
        print $fh $popfile;
        close $fh;
        $popfn = MakeRelPath($popfn, $TEMPDIR);
        $popurl = "$REMOTETEMPDIR/$popfn";
    }
    else {
        # Encode population to a data: URI
        my $u = URI->new('data:');
        $u->data($popfile);
        $popurl = $u.'';
        $popfn = "$DISPATCHID-pop-$checksum";
    }

    # Prepare to dispatch
    $workunits{$wuid} = {
        id => $wuid, worker => $worker->{id}, nitems => $c,
        upload => $UPLOADURL,
        files => [ @{$socks{$client}{files}},
                   [ $checksum, $popurl, "$REMOTETEMPDIR/$popfn"] ],
        sent => Time::HiRes::time, duration => $duration,
    };
    print $distsock 'DISPATCH ',to_json($workunits{$wuid}),"\n";
    # Tell the server that we will issue no more work in the immediate future.
    print $distsock "NOMOREWORK\n" if GetNextIndividual(-1,-1) < 0;
}

# Assign an individual to a worker
sub AssignItem {
    my ($item, $worker) = @_;
    $items[$item]{sent}++;
    return sprintf("I %d %s", $item, $items[$item]{values});
}

# Let us try to send some items out
sub TrySending {
    return if GetNextIndividual(-1,-1) < 0;
    print $distsock "HAVEWORK\n" unless $askingforworkers;
    $askingforworkers = 1;
}

sub md5 {
    my ($fn) = @_;
    open F, '<', $fn or return undef;
    binmode F;
    my $md5 = Digest::MD5->new->addfile(*F)->hexdigest;
    close F;
    return $md5;
}

sub cat {
    my ($fn) = @_;
    my $buf = '';
    open F, '<', $fn or return undef;
    binmode F;
    1 while sysread(F, $buf, 512, length($buf));
    close F;
    return $buf;
}

1;
