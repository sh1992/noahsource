#!/usr/bin/perl
#
# animate-spectrograph.pl - Parse a ga-spectroscopy log file and
# generate an animation.
#
use warnings;
use strict;

my $dir = shift @ARGV or die "Usage: $0 <dir>\n";
rmdir $dir;
mkdir $dir or die "Can't make directory $dir: $!";

my $TEMPLATE = './template';
my %template = ();
foreach ( qw/var int/ ) {
    my $fn = $TEMPLATE.'.'.$_;
    open(IN, '<', $fn) or die "Can't read $fn: $!";
    $template{$_} = '';
    read(IN, $template{$_}, -s IN);
    close IN;
}

my @data = ();				# Generation data segments
my @info = ();				# Generation metadata info (fitness)
my $best = 0;
my $gen = 0;
while (<>) {
    # Read BEST lines, including continuations
    if ( m/^(BEST (\d+) \d+)?\s+GD (\d+)\s+(\d+)( .*orig\s*([-.0-9]+))?$/ && ($1 || $best) ) {
	$best = 1;
	$gen = $2 if $1;
	$data[$gen][$3] = $4;
	if ( $1 ) {
	    $info[$gen][0] = $gen;
	    $info[$gen][1] = $6;
	}
    }
    else { $best = 0 }
}
for ( my $i=0; $i < @data && $i < @info; $i++ ) {
    print "$info[$i][0]\t",join("\t",@{$data[$i]}),"\t$info[$i][1]\n";

    # Generate SPCAT input files
    my $fn = "$dir/$info[$i][0].";
    foreach ( keys %template ) {
	my $x = "$fn$_";
	open OUT, '>', $x or die "Can't write $x: $!";
	printf OUT ($template{$_}, @{$data[$i]});
	close OUT;
    }
    #system("'./spcat' '$fn'") and die "SPCAT fail. $!";
    system("sh ./spectrograph.sh '$fn"."cat' png 'gen $info[$i][0] / fitness $info[$i][1]'") and die "Plot fail. $!";
}

# Generate MPEG4 animation (Requires mplayer/mencoder)
# system('mencoder', 'mf://*.png', '-mf', 'fps=25', '-noskip',
#        '-o', 'animation.avi', '-ovc', 'lavc', '-lavcopts', 'vcodec=mpeg4');

# Generate GIF animation (Requires ImageMagick)
system('convert -delay 1x25 *.png animation.gif');
