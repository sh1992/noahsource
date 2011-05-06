#!/usr/bin/perl
#
# buildlinux.pl - Assemble self-contained distribution of distclient with perl
#
# Similar to buildwin32.pl, programs should be merged.
#
use Module::ScanDeps;
use File::Copy;
use File::Path;
use File::Basename;

die "You probably want to run this on Linux Perl instead.\n"
    unless $^O eq 'linux';

my $out = "./outlinux/";
my @programs = ('distclient.pl', 'distclientcli.pl'); #wrapper.pl, osprobe.pl
my @datafiles = ('server.conf', '../ga-spectroscopy-client', '../spcat');
my @extramodules = ();#'Win32::OLE');
push @INC, './lib';

print "Scanning dependencies...\n";
my $hash = scan_deps(files => [@programs], recurse => 1, compile => 1,
                     add_modules => [@extramodules]);
print "Copying dependencies...\n";
foreach my $f ( keys %$hash ) {
    InstallFromInc($f, dirname("$out/lib/$f"));
}
#print "Copying WX...\n";
#foreach my $f ( 'wxbase28u_gcc_*.dll', 'wxmsw28u_core_gcc_*.dll', 'wxmsw28u_adv_gcc_*.dll', 'mingwm10.dll' ) {
    #InstallFromInc("Alien/wxWidgets/msw_*/lib/$f", $out);
#}
print "Copying Perl...\n";
foreach my $f ( '/usr/bin/perl' ) { # wperl
    copy($f, $out) or die "Can't copy $f";
    chmod 0755, "$out/".basename($f)
}
print "Copying Application...\n";
foreach my $f ( @programs, @datafiles ) {
    copy($f, $out) or die "Can't copy $f";
    chmod 0755, "$out".basename($f) unless $f =~ m/\.conf/;
}
#print "Creating extra directories...\n";
#foreach my $f ( 'data', 'temp' ) {
#    mkdir("$out/$f");
#}
# Fetch WX DLLs

sub InstallFromInc {
    my ($f,$d) = @_;
    print "$f\n";
    my $src = undef;
    foreach my $dir ( @INC ) {
        my $x = "$dir/$f";
        if ( $f =~ m/\*/ ) { $x = glob $x; next unless $x }
        if ( -e $x ) { $src = $x; break }
    }
    die "Can't find $f in @INC" unless $src;
    my $dest = "$d/".basename($src);
    mkpath(dirname($dest));
    copy($src, $dest) or die "Can't copy $f";
}

#copy('c:\strawberry\perl\bin\perl.exe', '.');
#copy('c:\strawberry\perl\bin\wperl.exe', '.');
