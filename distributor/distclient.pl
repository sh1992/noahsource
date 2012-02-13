#!/usr/bin/perl
#
# distclient.pl - Distributed Computing Client
#
use threads;
use threads::shared;
use FindBin;
BEGIN { chdir $FindBin::Bin; push @INC, "$FindBin::Bin/lib" }
use Sys::Hostname;
use Digest::MD5;
use JSON;
use IO::Socket::INET;
use IO::Select;
use HTTP::Request;
use HTTP::Async;
use File::Temp;
use File::Copy;
use File::Spec;
use URI;
use Archive::Tar;
use warnings;
use strict;

my $HAVE_Win32_API = 0;
eval 'use Win32::API; $HAVE_Win32_API = 1';

our $VERSION = 20120210;
my $USERAGENT = "distclient.pl/$VERSION";

# Load server configuration
our ($HOST, $PORT);
our ($SERVERNAME,$SERVERDETAIL) = ('Thesis@home','Thesis-related computations for noah.anderson@ncf.edu.'); # TODO: From a configuration file
my $serverconffn = File::Spec->catfile($FindBin::Bin, 'server.conf');
if ( open F, '<', $serverconffn ) {
    my $buf = <F>;
    my $obj = eval { from_json($buf) } || {};
    if ( exists($obj->{host}) ) { $HOST = $obj->{host} }
    if ( exists($obj->{port}) ) { $PORT = $obj->{port} }
    close F;
}
if ( !$HOST || !$PORT ) {
    die "Could not load server configuration";
}

# Detect system information (number of processors)
my $processorcount = 1;
my $platform = '';
if ( $^O eq 'MSWin32' ) {
    die "Need Win32::API to collect system statistics on MSWin32"
        unless $HAVE_Win32_API;
    # On Win32, use GetSystemInfo to find out how many processors we have
    #
    # typedef struct { DWORD  dwOemId; DWORD  dwPageSize;
    #   LPVOID lpMinimumApplicationAddress; LPVOID lpMaximumApplicationAddress;
    #   DWORD  dwActiveProcessorMask; DWORD  dwNumberOfProcessors;
    #   DWORD  dwProcessorType; DWORD  dwAllocationGranularity;
    #   WORD  wProcessorLevel; WORD  wProcessorRevision; } SYSTEM_INFO;
    # void GetSystemInfo(LPSYSTEM_INFO lpSystemInfo);
    my $function = ( Win32::API->new('kernel32','GetNativeSystemInfo','P','V')
                     || Win32::API->new('kernel32','GetSystemInfo','P','V') )
        or die "Can't access GetSystemInfo";
    # Will this even work on 64-bit Perl?
    my $systemInfo = pack('L9',0,0,0,0,0,0,0,0,0);
    $function->Call($systemInfo);
    my $dwNumberOfProcessors = (unpack('L9', $systemInfo))[5];
    $processorcount = $dwNumberOfProcessors if ($dwNumberOfProcessors||0) > 0;
    # Determine processor architecture
    my $wProcessorArchitecture = (unpack('L9', $systemInfo))[0]>>16;
    if ( $wProcessorArchitecture == 9 ) { $platform = 'win64' } # 9 = AMD64
    else { $platform = 'win32' } # 0 = X86, 6=Itanium
}
elsif ( -r '/proc/cpuinfo' ) {
    # On Linux, use procfs to find out how many CPUs we have
    open F, '<', '/proc/cpuinfo' or die "Cannot open /proc/cpuinfo";
    while (<F>) {
        $processorcount = $1+1 if m/^processor\s*:\s*(\d+)/;
    }
    close F;
}
if ( $^O eq 'linux' ) {
    my $uname = `uname -m`;
    $uname =~ s/\s//g;
    $uname = 'x86' if $uname =~ m/^i[0-9]86$/;
    $uname = 'x86_64' if $uname =~ m/amd64|86[_-]64/;
    $platform = "linux-$uname" if $uname;
}
$platform ||= 'all';

# Use defaults on other systems. Consider using Sys::Info instead.
my $hostname = hostname;

print "$hostname has $processorcount processors\n";
my $myident = Digest::MD5::md5_hex($hostname); # FIXME: System UUID, etc.

our $THREADCOUNT = $processorcount;
#if ( $^O eq 'MSWin32' ) { }
#elsif ( $ENV{HOME} ) { }
#else { die "Cannot load user preferences file" }
print "Using $THREADCOUNT processors\n";

# Inhibit sleep on Win32
my $SetThreadExecutionState = undef;
if ( $^O eq 'MSWin32' ) {
    die "Need Win32::API to inhibit power saving on MSWin32"
        unless $HAVE_Win32_API;
    # On Win32, use SetThreadExecutionState to inhibit automatic powersaving
    #
    # typedef DWORD EXECUTION_STATE;
    # EXECUTION_STATE WINAPI SetThreadExecutionState(EXECUTION_STATE esFlags);
    $SetThreadExecutionState =
        Win32::API->new('kernel32','SetThreadExecutionState','N','N')
        or die "Can't access SetThreadExecutionState";
}
sub DoInhibit {
    # 1 = ES_SYSTEM_REQUIRED
    $SetThreadExecutionState->Call(1) if $SetThreadExecutionState;
}

# Initialize the temporary directory
my $tempdir = File::Temp::tempdir('distclient-temp-XXXXX',
                                  TMPDIR => 1, CLEANUP => 1);
print "Using temporary directory $tempdir\n";
chdir $tempdir;

my %work :shared = ();          # Workunit information
my @finishedwork :shared = ();  # Work units to be reported as completed
my @pendingwork :shared = ();   # Work units yet to be started on
my %status :shared = ();

my $downloading :shared = 0;    # Mutex for download phase
my $appfiles :shared = 0;       # Mutex for application files
my $appfn :shared = '';         # Current application filename
my $appversion :shared = 0;     # App "version" (threads reload on change)

our $clientthread = undef;      # Socket communication thread
our @workthreads;               # Worker threads
our $statusposter;              # Front-end's status callback function

if ( !caller ) {
    print "PID: $$\n";
    $statusposter = sub {
        my ($result) = @_;
        print to_json($result),"\n";
    };
    StartClient();
    sleep 100 while 1;
    OnExit();
    exit 0;
}

# Start the client worker threads and main socket thread
sub StartClient {
    for ( my $i = 1; $i <= $THREADCOUNT; $i++ ) {
        push @workthreads, threads->create(\&WorkThread, $i);
    }
    $clientthread = threads->create(\&SocketThread);
}

# Called by a thread to send a status message to the front-end
sub PostStatus {
    my $id :shared = shift(@_)||0;
    unless ( exists($status{$id}) ) {
        $status{$id} = shared_clone({ id => $id });
    }
    while ( defined( my $k = shift(@_) ) && defined( my $v = shift(@_) ) ) {
        $status{$id}{$k} = $v
    }
    if ( $statusposter ) {
        $statusposter->($status{$id});
    }
}

# Called by front-end to convert a status message into a
# textual representation
sub RenderStatus {
    my ($status) = @_;
    my $mode = $status->{mode}||'';
    my $str = 'Idle';
    if ( $mode eq 'STARTING' ) { $str = 'Starting up...' }
    elsif ( $mode eq 'CONNECTING' ) { $str = "Connecting..." }
    elsif ( $mode eq 'DISCONNECTED' ) {
        $str = $status->{error} ? "Cannot connect: $status->{error}" :
               'Disconnected';
        $str .= " ($status->{progress}sec)" if defined($status->{progress});
    }
    elsif ( $mode eq 'WAITING' )
        { $str = $status->{id} ? 'Idle' : 'Connected' }
    #elsif ( $mode eq 'STOPPING' ) { $self->Close(1); return }
    elsif ( $mode eq 'DOWNLOADING' or $mode eq 'UPLOADING' ) {
        $str = sprintf('%s file %d of %d...',
                       ($mode eq 'UPLOADING') ? 'Uploading' : 'Downloading',
                       $status->{progress}+1, $status->{range});
    }
    elsif ( $mode eq 'WORKING' )
        { $str = 'Computing '.($status->{id}||'(untitled)').'...' }
    elsif ( $mode eq 'ERROR' )
        { $str = 'Error: '.($status->{error}||'(unknown)') }
    elsif ( $mode eq 'FINISHED' ) { $str = 'Completed' }
    return $str;
}

my $exiting :shared = 0;
# Called by front-end to trigger a shut down the client
sub DoExit {
    my @threads = ($clientthread, @workthreads);
    $_->kill('SIGTERM') foreach @threads;
    $exiting = 1;
}

# Called by front-end when the program is about to exit
sub OnExit {
    DoExit() unless $exiting;
    my @threads = ($clientthread, @workthreads);
    my $i = 0; my $wait = 1;
    while ( $i < 20 && $wait ) {
        $wait = 0;
        foreach ( @threads ) {
            unless ( $_->is_joinable() ) {
                # Nudge any threads stuck in "Idle" mode.
                { lock(@pendingwork); cond_broadcast(@pendingwork) }
                threads->yield();
                $wait++;
            }
        }
        if ( $wait ) {
            print "Waiting...\n";
            select(undef,undef,undef,.25);
        }
        else { print "No wait\n" }
        $i++;
    }
    foreach ( @threads ) { # print "DETACH\n"; $_->detach() }
        if ( $_->is_joinable() ) { print "JOIN\n";$_->join() }
        #else { print "DETACH\n";$_->detach() }
    }
    chdir $FindBin::Bin; # To allow tempdir cleanup to take place
}

# Thread to handle socket communication with the server
sub SocketThread {
    my $sock;
    local $SIG{TERM} = sub {
        PostStatus(undef, mode => 'STOPPING');
        foreach my $id ( keys %work ) {
            my $json = to_json({id => $id, error => 'User abort'});
            print $sock "WORKFAILED $json\n";
        }
        print "Thread exiting\n";
        # Cleanup
        if ( $sock ) {
            close($sock);
        }
        threads->exit();
    };
    local $SIG{PIPE} = 'IGNORE';
    my $wait = 3;
    while ( 1 ) {
        PostStatus(undef, mode => 'CONNECTING');
        $sock = IO::Socket::INET->new(PeerAddr => $HOST, PeerPort => $PORT);
        if ( !$sock ) {
            my $error = $!;
            for ( my $i = $wait; $i > 0; $i-- ) {
                PostStatus(undef, mode => 'DISCONNECTED', error => $error,
                                  progress => $i);
                sleep 1;
            }
            $wait = int($wait*1.4); # Back off before retrying?
            next;
        }
        $wait = 3;
        { my $oldfh = select($sock); $| = 1; select($oldfh) }
        my $select = IO::Select->new($sock);
        PostStatus(undef, mode => 'WAITING');
        my $sockbuf = '';
        my $lastseen = 0;
    SOCKLOOP:
        while ( 1 ) {
            while ( $select->can_read(.25) ) {
                my $bytes = sysread($sock, $sockbuf, 512, length($sockbuf));
                if ( !$bytes ) {
                    close($sock);
                    $sock = undef;
                    PostStatus(undef, mode => 'DISCONNECTED',
                               error => defined($bytes) ? 'Retrying' : $!);
                    last SOCKLOOP;
                }
                $lastseen = 0;
                while ( $sockbuf =~ s/^(.*?)\r*\n// ) {
                    my $l = $1;
                    if ( $l =~ m/^HELLO/ ) {
                        print $sock "HELLO $VERSION $hostname $myident\n";
                        print $sock "THREADS $THREADCOUNT\n";
                    }
                    elsif ( $l =~ m/^WORK (.+)/ ) {
                        my $json = $1;
                        my $obj = from_json($json); # FIXME: Crashproof
                        #print $sock "WORKREJECTED\n";
                        { lock(%work); $work{$obj->{id}} = shared_clone($obj) }
                        {
                            lock(@pendingwork);
                            push @pendingwork, $obj->{id};
                            cond_signal(@pendingwork);
                        }
                        #PostStatus($obj->{id}, mode => 'STARTING');
                        print $sock "WORKACCEPTED $obj->{id}\n";
                        DoInhibit();
                    }
                    elsif ( $l =~ m/^QUERYWORK/ ) {
                        my @k = keys %work;
                        my $json = to_json(\@k);
                        print $sock "WORKING $json\n";
                    }
                    elsif ( $l =~ m/^PING/ ) {
                        print $sock "PONG\n";
                    }
                    elsif ( $l =~ m/^PONG/ ) { }
                    elsif ( $l =~ m/^GOAWAY\s*(.*)/ ) {
                        PostStatus(undef, mode => 'ERROR', error => $1 ||
                                   'Rejected by server, no reason given.');
                    }
                    #print "$l\n"; # FIXME
                }
            }
            while ( 1 ) {
                my $w = undef;
                { lock(@finishedwork); $w = shift @finishedwork }
                last unless $w;
                my $cmd = 'WORKFINISHED';
                $cmd = 'WORKFAILED' if exists($w->{error}) && $w->{error};
                delete $status{$w->{id}};
                delete $work{$w->{id}};
                print $sock "$cmd ", to_json($w), "\n";
                #PostStatus(undef);
            }
            # Check ping time
            $lastseen++;
            if ( $lastseen == 2400 ) { print $sock "PING\n" }
            elsif ( $lastseen >= 2500 ) {
                close($sock);
                $sock = undef;
                PostStatus(undef, mode => 'DISCONNECTED',
                           error => 'Timed out waiting for a ping reply.');
                last SOCKLOOP;
            }
            # FIXME: Trap unexpected errors from threads
        }
        # Disconnected. Reconnect now.
        sleep 1;
    }
}

sub WorkThread {
    my ($thread) = @_;
    my $id;
    local $SIG{TERM} = sub {
        print "Worker exiting\n";
        # Cleanup
        threads->exit();
    };
    local $SIG{PIPE} = 'IGNORE';
    my %app = ();
    while ( 1 ) {
        { # Wait for work
            lock(@pendingwork);
            cond_timedwait(@pendingwork, time()+2)
                until @pendingwork or $exiting;
            threads->exit() if $exiting;
            $id = shift @pendingwork;
        }
        my $obj = $work{$id};
        #$obj->{thread} = $thread;
        #{ lock(@threadjobs); $threadjobs[$thread] = $id }
        eval {
            PostStatus($id, mode => 'STARTING', thread => $thread);
            # Check files
            DownloadFiles($id);
            # Get read-lock on application files
            {
                lock($appfiles);
                cond_wait($appfiles) until $appfiles >= 0;
                $appfiles++
            }
            my @outfiles;
            my $rc =eval {
                LocalLoadApplication($id, \%app);
                my $workfunc = \&{$app{work}};
                @outfiles = $workfunc->($id, $work{$id});
                1;
            };
            { lock($appfiles); $appfiles--; cond_broadcast($appfiles) }
            unless ( $rc ) {
                die if $@ =~ m/WORKFAIL/;   # We've already reported failure
                WorkFail($id, "Starting work: $@");
            }
            # Upload results
            PostStatus($id, mode => 'UPLOADING', progress => 0,
                       range => scalar(@outfiles));
            my $reply = { id => $id, files => [UploadFiles($id, \@outfiles)] };
            #CleanupFiles($id);
            #PostStatus($id, mode => 'FINISHED', progress => 1, range => 1);
            PostStatus($id, mode => 'WAITING', progress => 0, range => 1);
            { lock(@finishedwork); push @finishedwork, shared_clone($reply) }
        };
        #print "Finished\n";
        #{ lock(@threadjobs); $threadjobs[$thread] = undef if $threadjobs[$thread] eq $id };
    }
}

sub WorkFail {
    my ($id, $message) = @_;
    PostStatus($id, mode => 'ERROR', error => $message);
    print "$message\n";
    lock(@finishedwork);
    push @finishedwork, shared_clone({ id => $id, error => $message });
    die "WORKFAIL";
}

sub md5 {
    my ($fn) = @_;
    open F, '<', $fn or return undef;
    binmode F;
    my $md5 = Digest::MD5->new->addfile(*F)->hexdigest;
    close F;
    return $md5;
}

sub urlhosthack {
    my ($url) = @_;
    # Special hack: URLs with null hostnames (http://:9990/foo or
    # http:///foo) will default the hostname to that of the distributed
    # server, e.g. http://server:9990/foo and http://server/foo .
    $url =~ s/^(\w+:\/\/)([\:\/])/$1$HOST$2/;
    return $url;
}

sub DownloadFiles {
    my ($id) = @_;
    my $obj = $work{$id};
    my $async = HTTP::Async->new();
    my %fetch = (); my $nfiles = 0;
    my $got = 0;
    my $appfn = '';
    my $appchanged = 0;
    lock($downloading); # Prevent races
    foreach my $f ( @{$obj->{files}} ) {
        my ($valid, $url, $fn) = @$f;
        if ( IsApplication($fn) ) {
            WorkFail($id,"Workunit provided multiple applications") if $appfn;
            $appfn = $fn;
        }
        elsif ( $fn =~ m/^(([a-z]+)\/([-_.a-zA-Z0-9]+))$/ and
                ( !-e $2 or -d $2 ) ) {
            my ($dir,$basename) = ($2,$3);
            mkdir $dir unless -d $dir;
        }
        else { return WorkFail($id, "Invalid filename $fn") }
        my $checksum = md5($fn);
        next if defined($checksum) and $checksum eq $valid;
        # Checksum did not match, so download the file
        $nfiles++;
        if ( $url =~ m/^data:/i ) {
            my $u = URI->new($url);
            # FIXME: Integrate to avoid code duplication
            open F, '>', $fn or return WorkFail($id, "Cannot save $fn");
            binmode F;
            print F $u->data;
            close F or return WorkFail($id, "Cannot close saved $fn");
            # Verify checksum
            my $checksum = md5($fn);
            if ( $checksum ne $valid ) {
                return WorkFail($id, "Cannot verify checksum for $fn");
            }
        }
        else {
            my $req = HTTP::Request->new(GET => urlhosthack($url));
            $req->user_agent($USERAGENT);
            my $reqid = $async->add($req); # FIXME data: URI
            $fetch{$reqid} = [$valid, $fn];
        }
    }
    # Check application
    WorkFail($id, "Workunit provided no application") unless $appfn;
    unless ( $nfiles ) {
        CheckApplication($id, $appfn);
        return;
    }
    PostStatus($id, mode => 'DOWNLOADING', progress => 0, range => $nfiles);
    while ( my ($response,$respid) = $async->wait_for_next_response ) {
        # Update progress bar
        $got++;
        PostStatus($id, progress => $got);
        # Find our information about this request
        my ($valid, $fn) = @{$fetch{$respid}};
        delete $fetch{$respid};
        # Check response is OK
        if ( !$response->is_success ) {
            my $code = $response->code;
            return WorkFail($id, "HTTP $code while downloading $fn");
        }
        # Save response
        open F, '>', $fn or return WorkFail($id, "Cannot save $fn");
        binmode F;
        print F $response->decoded_content;
        close F or return WorkFail($id, "Cannot close saved $fn");
        # Verify checksum
        my $checksum = md5($fn);
        if ( !defined($checksum) or ($checksum ne $valid) ) {
            return WorkFail($id, "Cannot verify checksum for $fn");
        }
        $appchanged = 1 if $fn eq $appfn;
    }
    CheckApplication($id, $appfn, $appchanged);
}

sub CheckApplication {
    my ($id, $myappfn, $appchanged) = @_;
    # Check if we need to change apps
    if ( !$appfn || $appchanged || $appfn ne $myappfn ) {
        # Get write lock on application
        {
            lock($appfiles);
            cond_wait($appfiles) until $appfiles == 0;
            $appfiles--
        }
        my $rc = eval {
            ## The old app shouldn't be able to prevent a switch, so ignore
            ## any errors it may produce.
            #eval { my $func = \&{$app{stop}}; $func->() } if $app{stop};
            # Unpack the new app
            UnpackApplication($myappfn);
            $appfn = $myappfn;
            ## Start the new app
            #if ( $app{start} ) {
            #    my $func = \&{$app{start}};
            #    $func->();
            #}
            1;
        };
        $appversion++;
        $appversion = -1 unless $rc;
        # Release lock on application
        { lock($appfiles); $appfiles++; cond_broadcast($appfiles) }
        WorkFail($id, "Application change failed: $@") unless $rc;
    }
}

sub LocalLoadApplication {
    my ($id, $app) = @_;
    WorkFail($id, "No app loaded") if $appversion == -1;
    return if defined($app->{appversion}) and $appversion == $app->{appversion};
    print "Loading app version $appversion\n";
    my $appobj = do 'app.pl'
        or WorkFail($id, "Application code could not be loaded: $@ $!");
    # Remove old items from app hash
    UnsetApplication();
    # Insert new items into app hash
    foreach ( keys %$appobj ) { $app->{$_} = $appobj->{$_} }
    $app->{appversion} = $appversion;
    # Sanity check
    die "Application code provides no work function" unless $app->{work}
}

sub IsApplication { return $_[0] =~ m/^([-_a-zA-Z0-9.]+)\.app$/ }

sub UnsetApplication {
    my ($x) = @_;
    my @x = keys %$x;
    for ( @x ) { delete $x->{$_} }
}

sub UnpackApplication {
    my ($fn) = @_;
    # Unpack the archive
    my $tar = Archive::Tar->new();
    $tar->read($fn) or die "Can't open $fn: $!";
    foreach my $f ( $tar->list_files() ) {
        my @components = split /[\\\/]+/, $f;
        shift @components while @components and ($components[0] eq '.' or $components[0] eq '');
        next unless @components;
        my $filearch = $components[0];
        $components[0] = '.';
        # Check if file is in correct directory
        next unless $filearch eq 'all' or $filearch eq 'any' or
                    $filearch eq $platform;
        # Verify directory components to ensure there are no forbidden
        # characters or words
        foreach ( @components ) {
            if ( $_ =~ m/[\/\\?%*:|"<>\$\000-\037]|^(CON|PRN|AUX|NUL|COM\d+|LPT\d+)($|\.)|^\.\.|^\s*$/i ) {
                die "Application archive contains invalid filename: $f";
            }
        }
        # Extract the file
        $tar->extract_file($f, join('/', @components)) or die "Could not extract $f from app: $!";
    }
}

sub UploadFiles {
    my ($id, $outfiles) = @_;
    my $url = $work{$id}{upload};
    my $async = HTTP::Async->new();
    my %put = ();
    my $got = 0;
    my @reply = ();
    foreach my $f ( @$outfiles ) {
        my $buf = '';
        open F, '<', $f or WorkFail($id, "Failed to read output file $f");
        binmode F; # FIXME buffered IO?
        1 while sysread(F, $buf, 512, length($buf));
        close F;
        my $checksum = Digest::MD5::md5_hex($buf);
        if ( $url =~ m/^data:/ ) {
            # "Upload" to data: URI. Ideally LWP could do this for me,
            # so I wouldn't have to duplicate code.
            my $u = URI->new('data:');
            $u->data($buf);
            $got++;
            PostStatus($id, progress => $got);
            push @reply, [$checksum, $f, $u.''];
            unlink $f; # Delete successfully uploaded file
        }
        else {
            my $req = HTTP::Request->new(PUT => urlhosthack($url));
            $req->user_agent($USERAGENT);
            $req->content($buf);
            $req->encode('gzip') if length($buf) > 1024;
            my $reqid = $async->add($req);
            $put{$reqid} = [$checksum, $f];
        }
    }
    while ( my ($response,$respid) = $async->wait_for_next_response ) {
        # Update progress bar
        $got++;
        PostStatus($id, progress => $got);
        # Find our information about this request
        my $resp = $put{$respid};
        delete $put{$respid};
        # Check response is OK
        if ( !$response->is_success ) {
            my $code = $response->code;
            return WorkFail($id, "HTTP $code while uploading $resp->[1]");
        }
        unlink $resp->[1]; # Delete successfully uploaded file
        my $c = $response->content;
        $c =~ s/\s//g;
        push @reply, [@$resp, $c];
    }
    return @reply;
}

# App is responsible for cleaning up transient files (FIXME?)
#sub CleanupFiles {
#    my ($id) = @_;
#    my $obj = $work{$id};
#    foreach my $f ( @{$obj->{files}} ) {
#        my ($valid, $url, $fn) = @$f;
#        # FIXME: The following is too much of a hack, need better scheme.
#        #unlink $fn if $fn =~ m/^temp/;
#    }
#}

1;
