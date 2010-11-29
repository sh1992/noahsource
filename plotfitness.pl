#!/usr/bin/perl
#
# plotfitness.pl - Plot a graph of fitness as a function of generation.
# Takes ga-spectroscopy log files as input.
#

die "Usage: $0 <files>\n" unless @ARGV;
foreach $fn ( @ARGV ) {
    if ( $fn =~ m/\.bz2$/ )
        { open F, '-|', 'bzip2', '-d', $fn or die "bzip2 -d $fn: $!" }
    else { open F, '<', $fn or die "$fn: $!" }
    my @gens = ();
    my $hasdynmut = 0;
    my $ytop = 0;
    while (<F>) {
        if ( m/DYNM (\d+) (?:AVG\s+(-?[.0-9]+))?(\s+A\s+(-?[.0-9]+)\s+B\s+(-?[.0-9]+)\s+D\s+(-?[.0-9]+)\s+R\s+(-?[.0-9]+))?/ ) {
            my ($gen,$mean,$x,$a,$b,$d,$r) = ($1,$2,$3,$4,$5,$6,$7);
            $hasdynmut = 1 if $x;
            $gens[$gen]{avg} = $mean if defined($mean);
            $gens[$gen]{leading} = $a;
            $gens[$gen]{trailing} = $b;
            $gens[$gen]{difference} = $d;
            $gens[$gen]{mutationrate} = $r;
        }
        elsif ( m/BEST (\d+).+orig\s+(-?[.0-9]+)/ ) {
            my ($gen,$max) = ($1,$2);
            $gens[$gen]{gen} = $gen;
            $gens[$gen]{best} = $max;
            $ytop = int($max);
        }
        elsif ( m/AVRG (\d+)\s+(-?[.0-9]+)/ ) { $gens[$1]{avg} = $2 } # Old style
    }
    $ytop++;

    $outfn = $fn;
    die "Invalid filename format: $fn\n" unless $outfn =~ s/\.log(\.crushed)?$/-fitness.png/;

    open G, '|-', 'gnuplot';
    print G <<END;
set term png size 1200,480
set output "$outfn"
set xrange [0:*]
#set yrange [-45:$ytop]
set y2range [0:3]
set y2tics 0,1,1
set grid y2tics
set xlabel "Generation"
set ylabel "Fitness (0 is best)"
set y2label "Mutation rate, 0-1"
set style data lines
set title "$fn"
END
    print G 'plot "-" title "Average Fitness", "-" title "Best Fitness"',$hasdynmut ? ', "-" title "Leading Value", "-" title "Trailing Value", "-" title "Mutation Rate" axes x1y2 lt -1' : '',"\n";

    foreach my $key ( qw/avg best/, $hasdynmut ? qw/leading trailing mutationrate/: () ) {
        foreach ( @gens ) {
            print G "$_->{gen} $_->{$key}\n";
        }
        print G "e\n";
    }
    close G;
}
