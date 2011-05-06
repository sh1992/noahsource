#!/usr/bin/perl
#
# buildwin32.pl - Assemble self-contained distribution of distclient with perl
#
# Similar to buildlinux.pl, programs should be merged.
#
use Module::ScanDeps;
use File::Copy;
use File::Path;
use File::Basename;

die "You probably want to run this on Win32 Perl instead.\n"
    unless $^O eq 'MSWin32';

my $out = "./out/";
my @programs = ('distclient.pl', 'distclientwx.pl'); #wrapper.pl, osprobe.pl
my @datafiles = ('wrapper/distclient.exe', 'server.conf', 'box.ico',
                 '../ga-spectroscopy-client.exe', '../spcat.exe');
my @extramodules = ();#'Win32::OLE');
push @INC, './lib';

print "Scanning dependencies...\n";
my $hash = scan_deps(files => [@programs], recurse => 1, compile => 1,
                     add_modules => [@extramodules]);
print "Copying dependencies...\n";
foreach my $f ( keys %$hash ) {
    InstallFromInc($f, dirname("$out/lib/$f"));
}
print "Copying WX...\n";
foreach my $f ( 'wxbase28u_gcc_*.dll', 'wxmsw28u_core_gcc_*.dll', 'wxmsw28u_adv_gcc_*.dll', 'mingwm10.dll' ) {
    InstallFromInc("Alien/wxWidgets/msw_*/lib/$f", $out);
}
print "Copying Perl...\n";
foreach my $f ( 'perl.exe', 'perl*.dll' ) { # wperl
    InstallFromInc("../bin/$f", $out);
}
copy('manifest.xml', "$out/perl.exe.manifest");
print "Copying Application...\n";
foreach my $f ( @programs, @datafiles ) {
    copy($f, $out) or die "Can't copy $f";
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
