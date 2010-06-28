#!/usr/bin/perl
#
# animate-spectrograph.pl - Parse a ga-spectroscopy log file and
# generate an animation.
#
use warnings;
use strict;

my $ANIMATE_SPECTROGRAPH = 0;
my $ANIMATE_HISTOGRAM = 1;

my $dir = shift @ARGV or die "Usage: $0 <dir>\n";
rmdir $dir;
mkdir $dir or die "Can't make directory $dir: $!";

my $TEMPLATE = './template-lite';
my %template = ();
if ( $ANIMATE_SPECTROGRAPH ) {
    foreach ( qw/var int/ ) {
	my $fn = $TEMPLATE.'.'.$_;
	open(IN, '<', $fn) or die "Can't read $fn: $!";
	$template{$_} = '';
	read(IN, $template{$_}, -s IN);
	close IN;
    }
}

my @histogram = ();
my @histtotal = ();
my @data = ();				# Generation data segments
my @info = ();				# Generation metadata info (fitness)
my $best = 0;
my $item = 0;
my $gen = 0;
my @gens = ();
while (<>) {
    # Read BEST lines, including continuations
    if ( m/^(BEST (\d+) \d+)?\s+GD (\d+)\s+(\d+)( .*orig\s*([-.0-9]+))?$/
	 && ($1 || $best) ) {
	$best = 1; $item = 0;
	my $newgen = $1 ? 1 : 0;
	if ( $newgen ) { $gen = $2; push @gens, $gen }
	$data[$gen][$3] = $4;
	if ( $newgen ) {
	    $info[$gen][0] = $gen;
	    $info[$gen][1] = $6;

	    if ( $gen%20 == 0 ) {
		# Generate histogram
		my $x = "$dir/hist-$gen.m";
		open OUT, '>', $x or die "Can't write $x: $!";
		my @a = ();
		for ( my $i = 0; $i < @{$histogram[$gen]}; $i++ ) {
		    print OUT "V",$i+1," = [\n",
		      join("\n",keys %{$histogram[$gen][$i]}),"\n];\n";
		}
		print OUT <<END;
clf; hold on;
ylim([0 64]);
xlim([0 2^32]);
intvl = 2^23;
hist(V1,[0:intvl:2^32]);
hist(V2,[intvl/3:intvl:2^32]);
hist(V3,[2*intvl/3:intvl:2^32]);
hold off;
h = findobj(gca,'Type','patch');
set(h(1),'FaceColor','r','EdgeColor','r');
set(h(2),'FaceColor','g','EdgeColor','g');
set(h(3),'FaceColor','b','EdgeColor','b');
legend('A','B','C'); title("Generation $gen");
print $dir/hist-$gen.png -dpng
END
		close OUT;
	    }
	}
    }
    elsif ( m/^((?:ITEM|FITM) (\d+) \d+)?\s+GD (\d+)\s+(\d+)/
	    && ($1 || $item) ) {
	my $newgen = $1 ? 1 : 0;
	$gen = $2 if $newgen;

	$best = 0; $item = 1;
	$histogram[$gen][$3]{$4}++;
	$histtotal[$gen][$3]++;
    }
    else { $best = 0; $item = 0 }
}

if ( $ANIMATE_SPECTROGRAPH ) {
    for ( my $i=0; $i < @data && $i < @info; $i++ ) {
	print "$info[$i][0]\t",join("\t",@{$data[$i]}),"\t$info[$i][1]\n";

	# Generate SPCAT input files
	my $fn = "$dir/spcat-$info[$i][0].";
	foreach ( keys %template ) {
	    my $x = "$fn$_";
	    open OUT, '>', $x or die "Can't write $x: $!";
	    printf OUT ($template{$_}, @{$data[$i]});
	    close OUT;
	}
	#system("'./spcat' '$fn'") and die "SPCAT fail. $!";
	system("sh ./spectrograph.sh '$fn"."cat' png 'gen $info[$i][0] / fitness $info[$i][1]'") and die "Plot fail. $!";
    }
    open F, '>', "$dir/spcat-files.txt" or die;
    print F "spcat-$_.png\n" foreach @gens;
    close F;
    system('mencoder', "mf://\@$dir/spcat-files.txt", '-mf', 'fps=25',
           '-noskip', '-o', "$dir/spcat.avi", '-ovc', 'lavc',
           '-lavcopts', 'vcodec=mpeg4');
}

if ( $ANIMATE_HISTOGRAM ) {
    system("sh -c 'for x in $dir/hist-*.m; do DISPLAY= octave \$x; done'");
    open F, '>', "$dir/hist-files.txt" or die;
    print F "hist-$_.png\n" foreach @gens;
    close F;
    system('mencoder', "mf://\@$dir/hist-files.txt", '-mf', 'fps=6', '-noskip',
       '-o', "$dir/hist.avi", '-ovc', 'lavc', '-lavcopts', 'vcodec=mpeg4');
}

# Generate MPEG4 animation (Requires mplayer/mencoder)
# system('mencoder', 'mf://*.png', '-mf', 'fps=25', '-noskip',
#        '-o', 'animation.avi', '-ovc', 'lavc', '-lavcopts', 'vcodec=mpeg4');

# Generate GIF animation (Requires ImageMagick)
#system('convert -delay 1x25 '.$dir.'/*.png animation.gif');
