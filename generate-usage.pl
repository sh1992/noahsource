#!/usr/bin/perl
#
# generate-usage.pl - Generate usage messages from source comments.
#

foreach my $f ( @ARGV) {
    $f =~ m/(?:^|\/)([-0-9a-z_]+)\.c$/i;
    (warn("Ignoring $f\n"),next) unless $1;
    $out = $1.'.usage.h';
    $var = $1; $var =~ s/[^a-z]//g;
    open O, '>', $out;
    print O 'char *'.$var.'_usage = "';
    open F, '<', $f;
    my $x = join('',<F>);
    close F;
    $x =~ s/\r//g;				# Remove DOS line endings
    foreach ( $x =~ m/(\/\*\*\s*-.+?\*\/)/gs ) {
	s/^\/\*//; s/\s*\*\/$//;
	s/^\s*\*[^\S\n]*//gm;
	s/^([^\n]+)\s+//g;
	$m = "\n$1\n$_\n";
	$m =~ s/\n/\\n/g; $m =~ s/"/\\"/g;
	print O $m;
    }
    print O "\";\n";
}
