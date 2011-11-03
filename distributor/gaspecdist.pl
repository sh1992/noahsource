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

my ($DHOST, $DPORT) = ('localhost', 9933);  # Connect to distributor
my ($LHOST, $LPORT) = ('localhost', 2222);  # Listen for ga-spectroscopy
my ($WHOST, $WPORT) = ('localhost', 9990);  # HTTP host and port

# Load HTTP server information from server.conf
open F, '<', 'server.conf' or die "Cannot load server.conf: $!";
my $serverconf = from_json(join '', <F>);
close F;
$WHOST = $serverconf->{host} if $serverconf->{host};

my $SPECDIR = '..';                         # CWD for ga-spectroscopy processes
my $DATADIR = '../data';                    # Location of data directory
my $DATAURL = "http://$WHOST:$WPORT/spec/data"; # URL of the same
my $TEMPDIR = '../temp';                    # Location of a temporary location
my $TEMPURL = "http://$WHOST:$WPORT/spec/temp"; # URL of the same
my $APPDIR = '.';
my $APPNAME = 'ga-spectroscopy.app';
my $APPURL = "$TEMPURL/$APPNAME";
my $INBOXDIR = '../inbox';
#my $UPLOADURL = "http://$WHOST:$WPORT/spec/upload"; # URL of uploader
my $UPLOADURL = 'data:';
# Name of data and temporary directories on workers
my ($REMOTEDATADIR,$REMOTETEMPDIR) = ('data','temp');
my $WUDURATION = 10;

my $JOBSENDERTOKEN = Digest::MD5::md5_hex('DISTJOBS', 'gaspecdist', 'aptronym');
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
                        # FIXME: Do some sanity checks
                        my $source = -1;
                        my $gasock = undef;
                        my $receivedtime = Time::HiRes::time;
                        my $fatal = 0;
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
                                    next unless $items[$item]{workunit} eq $reply->{id};
                                    $items[$item]{fitness} = $fitness;
                                    $items[$item]{received}++;
                                    $source = $items[$item]{source};
                                    if ( !defined($source) || $source < 0 )
                                        { warn "No gasock" }
                                    $gasock = $socks{$source}{sock};
                                    printf $gasock "F %d %s\n",
                                        $items[$item]{origindex},
                                        $items[$item]{fitness};
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
                        my $foundothers = 0;
                        my $foundunreturned = 0;
                        foreach my $item ( @items ) {
                            next unless $item;
                            # FIXME: Assumes numeric source IDs
                            next if defined($source) && $source >= 0 and
                                ( $item->{source} != $source );
                            $foundothers = 1;
                            next unless $item->{workunit} eq $reply->{id};
                            next unless $item->{sent} > $item->{received};
                            # Try sending again
                            $foundunreturned = 1;
                            $item->{sent} = 0; $item->{received} = 0;
                            $item->{workunit} = ''; # FIXME multiple workunits
                        }
                        if ( !$foundothers ) {
                            if ( !$gasock ) { warn "No gasock" }
                            print $gasock "DONE\n";
                        }
                        # Update workerspeed
                        if ( $source && !$foundunreturned ) {
                            my $wu = $workunits{$reply->{id}};
                            my $n = $wu->{nitems}/($receivedtime-$wu->{sent});
                            $n = 1/60 if $n < 1/60;
                            # FIXME: max speed?
                            my $ws = $socks{$source}{workerspeed};
                            $ws->{$wu->{worker}} = $n;
                        }
                        # Dispatch more work
                        delete $workunits{$reply->{id}};
                        TrySending();
                    }
                }
            }
            elsif ( $sock eq $galisten ) {
                my $client = $sock->accept();
                my $id = fileno($client);
                $uniqclientid++;
                $socks{$id} = { id => $id, sock => $client, buf => '',
                                uniqid => $uniqclientid,
                                config => undef, configfile => undef,
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

sub get_app {
    my $appfile = "$APPDIR/$APPNAME";
    copy($appfile, "$TEMPDIR/$APPNAME")
        or warn "Can't copy $appfile to temp: $!";
    return (md5($appfile)||'', $APPURL, $APPNAME);
}

sub HandleSocket {
    my ($id) = @_;
    while ( $socks{$id}{buf} =~ s/^(.*?)\r*\n// ) {
        my $l = $1;
        print "G$id: $l\n" unless $l =~ m/^I /;
        if ( $l =~ m/^V .+$/ ) {
            $socks{$id}{config} .= "$l\n";
            # Save config to file
            (my $fh, $socks{$id}{configfile}) =
                File::Temp::tempfile("$DISPATCHID-config-XXXXX", DIR => $TEMPDIR,
                                     OPEN => 1, EXLOCK => 0, UNLINK => 0);
            print $fh $socks{$id}{config};
            close $fh;
            my $checksum = Digest::MD5::md5_hex($socks{$id}{config});
            my $fn = MakeRelPath($socks{$id}{configfile}, $TEMPDIR);
            push @{$socks{$id}{files}},
                         [$checksum, "$TEMPURL/$fn", "$REMOTETEMPDIR/$fn"];
        }
        elsif ( $l =~ m/^G (\d+)/ ) {
            $socks{$id}{gen} = $1;
            $socks{$id}{wucount} = 'aaa';
        }
        elsif ( $l =~ m/^I (\d+) (.+)$/ ) {
            my ($origindex, $values) = ($1, $2);
            AppendItem({source => $id, origindex => $origindex, workunit => '',
                        values => $values, fitness => undef});
        }
        elsif ( $l =~ m/^DISPATCH/ ) {
            # Prepare this generation to be sent
            my $foundothers = 0;
            foreach ( @items ) {
                if ( defined($_) and $_->{source} == $id ) {
                    $_->{sent} = 0;
                    $foundothers = 1;
                }
            }
            # Received empty population (entire population was in the cache)
            if ( !$foundothers ) {
                my $gasock = $socks{$id}{sock};
                print $gasock "DONE\n";
            }
            else { TrySending() }
        }
        elsif ( !$socks{$id}{configfile} ) {
            if ( $l =~ m/^(CFG[A-Z0-9] (match|template|drfile) )(.+)/ ) {
                my ($lineprefix, $type, $fn) = ($1, lc $2, $3);
                # Determine our destination filename
                my (undef,undef,$outfile) = File::Spec->splitpath($fn);
                $outfile = "$REMOTEDATADIR/$outfile";
                # .INT+.VAR files
                my @suffixes = ($type eq 'template') ? ('.int','.var') : ('');
                # Where is this data file really?
                foreach my $suffix ( @suffixes ) {
                    my $realfn = File::Spec->rel2abs($fn.$suffix, $SPECDIR);
                    my $wwwfn = MakeRelPath($realfn, $DATADIR);
                    my $checksum = md5($realfn);
                    if ( !$checksum or !-e $realfn ) {
                        warn "Error checksumming $fn=>$realfn or file does not exist (ID=$id config): $!";
                        next;
                    }
                    if ( $wwwfn =~ m/^\.\./ ) {
                        warn "Data file $fn is not in $DATADIR";
                        next;
                    }
                    push @{$socks{$id}{files}},
                         [$checksum, "$DATAURL/$wwwfn", "$outfile$suffix"];
                }
                $l = $lineprefix.$outfile;
            }
            $socks{$id}{config} .= "$l\n";
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
    for ( my $i = $from+1; $i < @items; $i++ ) {
        if ( $items[$i] && $items[$i]{sent} == $items[$i]{received} &&
             ( $client < 0 || $items[$i]{source} == $client ) &&
             $items[$i]{sent} == 0 ) {
             return $i;
         }
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
    my $ws = $socks{$client}{workerspeed};
    my $duration = $WUDURATION+int(5*rand());
    my $n = ( exists($ws->{$worker->{id}}) && $ws->{$worker->{id}} ) ?
            $ws->{$worker->{id}} : 10/$duration;
    $n *= $duration;

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
        $items[$i]{workunit} = $wuid; # FIXME: Multi-dispatched?
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
