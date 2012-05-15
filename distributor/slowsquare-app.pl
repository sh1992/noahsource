package DistClient::SlowSquare;     # Change SlowSquare to something unique
use threads::shared;
use warnings;
use strict;
sub Work {
    my ($id, $obj) = @_; # $id is workunit ID, $obj is workunit object
    my $infile = $obj->{files}[1][2];       # Filename of second input file
    open F, '<', $infile or die "Failed to read $infile";
    (my $number = <F>) =~ s/\s//g; # Read line from infile, removing non-digits
    close F;
    # Create output file
    my $TEMPDIR = File::Spec->catdir(File::Spec->curdir, 'temp');
    mkdir $TEMPDIR;
    (undef, my $outfile) =
        File::Temp::tempfile("temp-output-XXXXX", DIR => $TEMPDIR,
                             OPEN => 0, EXLOCK => 0, UNLINK => 0);
    # Update progress. range indicates number of progress steps.
    main::PostStatus($id, mode=>'WORKING', progress=>0, range=>$number);
    my $output = 0;
    for ( my $i = 0; $i < $number; $i++ ) {
        $output += $number; sleep 1;              # Do something
        main::PostStatus($id, progress => $i+1);  # Update progress
    }
    unlink $infile;             # We don't need the input file anymore
    # Write a result to the output file and return it
    open F, '>', $outfile or die "Failed to write $outfile";
    print F $output;
    close F;
    return $outfile; # Multiple files possible too: return $outfile, $logfile
}
{ work => __PACKAGE__."::Work" }; # Return name of our work function

