#
# gaspecclient.pl - Distributed Computing Client for ga-spectroscopy.
#
#
# This file is dynamically (re)loaded by each worker thread whenever the
# application changes. PERL THREADS ARE BUGGY: KEEP THIS CODE SIMPLE.
#
package DistClient::GaSpectroscopy;
use threads::shared;
use warnings;
use strict;

my $HAVE_Win32_Process = 0;
eval 'use Win32::Process; $HAVE_Win32_Process = 1';

sub Work {
    my ($id, $obj) = @_;
    #my $obj = $work{$id};
    my $TEMPDIR = File::Spec->catdir(File::Spec->curdir, 'temp');

    # Create input file from conf file and popfile
    my $conffile = $obj->{files}[-2][2];
    my $popfile = $obj->{files}[-1][2];
    my $nitems = 0;
    (my $fh, my $infn) =
        File::Temp::tempfile("temp-input-XXXXX", DIR => $TEMPDIR,
                             OPEN => 1, EXLOCK => 0, UNLINK => 0);
    foreach my $srcfn ( $conffile, $popfile ) {
        open IN, '<', $srcfn or return WorkFail($id, "Failed to read $srcfn");
        while ( <IN> ) {
            $nitems++ if m/^I /;
            print $fh $_;
        }
    }
    close IN;
    close $fh;
    # We can get rid of the population file now, since it's unique to this
    # workunit.
    unlink $popfile;

    # Create output file (FIXME: This should all happen in a real tempdir)
    (undef, my $outfn) =
        File::Temp::tempfile("temp-output-XXXXX", DIR => $TEMPDIR,
                             OPEN => 0, EXLOCK => 0, UNLINK => 0);

    # Start gaspec process
    my $i = 0;
    main::PostStatus($id, mode => 'WORKING', progress => 0, range => $nitems);
    my $logbuf = "ERROR LOG FOLLOWS (Something went wrong)\n";
    my $rc;
    {
        #my $quit = 0;
        my $pid = 0;
        local $SIG{TERM} = sub {
            # If we don't shut down the subprocess before stopping the thread,
            # Win32 Perl crashes.
            if ( $pid > 0 ) { print "Killing $pid\n"; kill 'TERM', $pid }
            close SPEC;
            print "Worker exiting (3)\n";
            threads->exit;
            #$quit = 1;
        };
        #print "$id Starting ./ga-spectroscopy-client \"$infn\" \"$outfn\"\n";
        my $program = File::Spec->catfile(File::Spec->curdir,
                                          'ga-spectroscopy-client');
        my $cmd = "$program \"$infn\" \"$outfn\"";
        $cmd = "nice -n19 $cmd" if $^O ne 'MSWin32';
        $pid = open SPEC, '-|', $cmd or WorkFail($id, 'Cannot start gaspec client');
        # Nice the process on MSWin32. Note that it would be better to do this
        # in CreateProcess...
        if ( $HAVE_Win32_Process ) {
            my $process;
            if ( Win32::Process::Open($process, $pid, 0) ) {
                $process->SetPriorityClass(&IDLE_PRIORITY_CLASS);
            }
        }
        { my $oldfh = select SPEC; $| = 1; select $oldfh }
        #print "$id Reading\n";
        while ( <SPEC> ) {
            #print "$id $_"; # FIXME: Do something with these messages
            if ( m/^F / ) {
                $i++;
                main::PostStatus($id, progress => $i);
            }
            $logbuf .= $_;
        }
        #print "$id gaspec done\n";
        $rc = close SPEC;
        unlink $infn; # Remove our temporary input file
        #if ( $quit ) { close SPEC; print "Worker exiting (2)\n"; threads->exit }
    }

    # Process finished
    if ( $rc ) { return $outfn }
    else { # Something went wrong, save the error log
        open F, '>', $outfn;
        print F $logbuf;
        close F;
        print $logbuf;
    }
    return $outfn;
}

# The below handlers are unimplemented in distclient.

#sub Start {
#    print "appstart\n";
#    # Do nothing
#}

#sub Stop {
#    print "appstop\n";
#    # Do nothing
#}


# Return function pointers to distclient
my $P = __PACKAGE__.'::';
{ work => $P."Work" }; # start => $P."Start", stop => $P."Stop"
