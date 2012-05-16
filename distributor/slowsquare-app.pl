package DistClient::SlowSquare;     # Change SlowSquare to something unique
use threads::shared;
use warnings;
use strict;

sub Work {
    my ($id, $obj) = @_; # $id is workunit ID, $obj is workunit object

    # Read the second input file (the first input file is the app)
    my $infile = $obj->{files}[1][2];
    open F, '<', $infile or die "Failed to read $infile";
    # Read first line from infile, convert to number
    my $number = (<F>+0);
    close F;

    # Create output file
    (undef, my $outfile) =
        File::Temp::tempfile("temp-output-XXXXX", DIR => '.',
                             OPEN => 0, EXLOCK => 0, UNLINK => 0);

    # Update progress. range indicates number of progress steps.
    main::PostStatus($id, mode=>'WORKING', progress=>0, range=>$number);

    # Perform computation
    my $output = 0;
    for ( my $i = 0; $i < $number; $i++ ) {
        # Do something
        $output += $number;
        sleep 1;
        # Update progress
        main::PostStatus($id, progress => $i+1);
    }

    # We don't need the input file anymore
    unlink $infile;

    # Write a result to the output file and return it
    open F, '>', $outfile or die "Failed to write $outfile";
    print F $output;
    close F;

    # Multiple files are possible too: return $outfile, $logfile
    return $outfile;
}

# Return the name of our work function
{ work => __PACKAGE__."::Work" };

