#
# python-app.pl - Distributed Computing Client app for Python programs.
#
# This file is dynamically (re)loaded by each worker thread whenever the
# application changes. PERL THREADS ARE BUGGY: KEEP THIS CODE SIMPLE.
#
package DistClient::PythonApp;
use threads::shared;
use warnings;
use strict;

use File::Spec;

# This file should be packaged in an app (as app.pl) along with a Python script
# called app.py. As explained in Appendix B of my thesis, apps are
# gzip-compressed tar files (similar to ZIP files) with a certain structure
# inside. (Specifically, they have directories all, win32, win64, linux-x86,
# and linux-x86_64. The contents of the all directory, along with the
# appropriate OS+architecture-specific directory, will be extracted by the
# distributed computing client to the current working directory.
# For example, your python script should probably be put in all, and SPCAT or
# other compiled binaries should be put in the platform-specific directory.
#
# If the current directory has the correct structure, the following command
# can be used to create a gzip-compressed tar file on a Linux/UNIX system:
#
#     tar czf ../somefilename.app .
#
# app.py will be run with all of the input filenames as command-line
# parameters; including the app. These are available in sys.argv[].
#
# app.py should output status information regularly (no less often than every
# few seconds) and may update the progress bar by outputting lines in this
# format:
#
#     DISTCLIENT STATUS 7/11
#     DISTCLIENT STATUS 9
#
# The first command sets both the progress and range (i.e. literally 7 out of
# 11 = 64%). The second command sets only the progress and uses the same range
# as previously set (i.e. 9 out of 11).
#
# At start time, app.py should determine the range and emit a line in the first
# format, with 0 progress (e.g. DISTCLIENT STATUS 0/123, where 123 is the
# determined range). This will set the progress bar to 0% and update the status
# display from "Starting..." to "Computing...".
#
# app.py should also output lines of the form
#
#     DISTCLIENT OUTPUT output/file.txt
#
# The files referenced by such lines will be uploaded in the order they appear.
#
# Remember, app.py should delete input files it does not expect to reuse for
# later workunits. Output files will be deleted automatically after uploading.

# Supported versions of Python (used during search, e.g. to check C:\Python2x).
# We don't guarantee that we will use one of these versions of Python.
# You should leave the empty string at the beginning.
my @versions = ('', '2', '2.7', '2.6', '2.5', '27', '26', '25');

# Try to find a copy of Python
my $PYTHON = FindPython();
die "Can't find Python anywhere" unless $PYTHON;

sub FindPython {
    # On Windows, Python is called python.exe, not python.
    my $WINDOWS = $^O eq 'MSWin32';
    my $extension = $WINDOWS ? '.exe' : '';

    # First, check the system PATH (directories searched for executables)
    my @dirs = File::Spec->path();
    if ( $WINDOWS ) {
        # On Windows, also try C:\Python27 and friends
        push @dirs, "C:\\Python$_" foreach @versions;
    }
    foreach my $dir ( @dirs ) {
        next unless -d $dir;
        # On Linux, it will be called python, python2, python2.7, or python2.6
        foreach my $ver ( @versions ) {
            my $fn = "$dir/python$ver$extension";
            #print "Trying $fn\n";
            if ( -x $fn ) {
                return $fn;
            }
        }
    }

    # This should have been sufficient if we're on Linux.
    return undef unless $WINDOWS;
    # It should also be sufficient on most Windows systems. Most people won't
    # have changed the install path.
    # FIXME: If it becomes necessary, can also check registry keys
    #     HKEY_LOCAL_MACHINE\SOFTWARE\Python\Pythoncore\2.x\InstallPath
    return undef;
}

# Process a workunit
sub Work {
    my ($id, $obj) = @_;
    my $rc;
    my $kill = 0;
    my $oldsigterm = $SIG{TERM};
    my @outputs = ();
    {
        my $pid = 0;
        # If we don't shut down the subprocess before stopping the thread,
        # Win32 Perl crashes.
        local $SIG{TERM} = sub { $kill = 2 };
        local $SIG{HUP} = sub { $kill = 1 };
        my $program = File::Spec->catfile(File::Spec->curdir, 'app.py');
        # The -u option causes Python to use unbuffered output. This
        # means that DISTCLIENT output lines will be available immediately.
        my $cmd = "\"$PYTHON\" -u \"$program\"";
        $cmd .= " \"$_->[2]\"" foreach @{$obj->{files}};
        # Running python via nice will cause it to run with low priority.
        $cmd = "nice -n19 $cmd" if $^O ne 'MSWin32';
        $pid = open PY, '-|', $cmd
            or main::WorkFail($id, 'Cannot start python');
        # Set low priority for the process on MSWin32.
        main::Win32Nice($pid);
        { my $oldfh = select PY; $| = 1; select $oldfh }
        while ( <PY> ) {
            if ( m/^DISTCLIENT STATUS (\d+)(?:\/(\d+))?/ ) {
                if ( defined($2) ) {
                    main::PostStatus($id, mode=>'WORKING', progress => $1,
                                          range => $2);
                }
                else { main::PostStatus($id, progress => $1) }
            }
            elsif ( m/^DISTCLIENT OUTPUT (.+?)\s*$/ ) {
                push @outputs, $1;
            }
            if ( $kill > 0 ) {
                if ( $pid > 0 ) { print "Killing $pid\n"; kill 'TERM', $pid }
                last;
            }
        }
        $rc = close PY;
    }
    $oldsigterm->() if $kill >= 2;
    if ( !$rc ) {
        main::WorkFail($id, "Python program did not exit with status 0");
        return;
    }
    # Return the list of output files
    return @outputs;
}

# Return the name of our work function
{ work => __PACKAGE__."::Work" };
