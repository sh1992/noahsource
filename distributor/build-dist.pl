#!/usr/bin/perl
#
# build-dist.pl - Assemble self-contained distribution of distclient with Perl
#
use Module::ScanDeps;
use File::Copy;
use File::Path;
use File::Basename;
use FindBin;
BEGIN { chdir $FindBin::Bin; push @INC, "$FindBin::Bin/lib" }

print "Building for $^O.\n";

my $out = "./out-$^O/";
my @programs = ('distclient.pl', 'distclientwx.pl', 'distclientplain.pl'); #wrapper.pl, osprobe.pl
my @datafiles = ('server.conf', 'box.ico');
my @extramodules = ();#'Win32::OLE');
push @Module::ScanDeps::IncludeLibs, @INC;

# Platform-specific files
push @programs, 'distclientcli.pl' if $^O ne 'MSWin32';
push @datafiles, 'wrapper/distclient.exe' if $^O eq 'MSWin32';

# Perl module dependencies
print "Scanning dependencies...\n";
my $hash = scan_deps(files => [@programs], recurse => 1, compile => 1,
                     add_modules => [@extramodules]);
#my $hash = {};

print "Copying dependencies...\n";
foreach my $f ( keys %$hash ) {
    next if $f =~ m/\.pdb$/i;
    InstallFromInc($f, dirname("$out/lib/$f"));
}

# Additional files
print "Copying Application...\n";
foreach my $f ( @programs, @datafiles ) {
    DoCopy($f, $out);
}

# On Windows, we need Perl, wxWidgets, the VC10 runtime, and manifest files.
if ( $^O eq 'MSWin32' ) {
    print "Copying Perl...\n";
    foreach my $f ( 'perl.exe', 'perl*.dll' ) { # wperl
        InstallFromInc("../bin/$f", $out);
    }

    print "Copying wxWidgets...\n";
    my $compiler = 'vc';
    my @wx = ('wxbase28u_'.$compiler.'_*.dll',
              'wxmsw28u_core_'.$compiler.'_*.dll',
              'wxmsw28u_adv_'.$compiler.'_*.dll');
    push @wx, 'mingwm10.dll' if $compiler eq 'gcc';
    foreach my $f ( @wx ) {
        InstallFromInc("Alien/wxWidgets/msw_*/lib/$f", $out);
    }

    # It's too bad I need this.
    print "Checking for VC10 runtime dependency...\n";
    foreach my $f ( "$out/perl.exe" ) {
        open F, $f or warn "Can't open $f: $!";
        binmode F;
        my $buf = '';
        1 while read F, $buf, 1024, length($buf);
        close F;
        if ( $buf =~ m/msvcr100/i ) {
            print "Copying VC10 runtime...\n";
            for my $x ( glob "msvcr100/*.dll" ) {
                DoCopy($x, $out);
            }
            last;
        }
    }

    # MANIFESTS FOR EVERYTHING! (Required to prevent resource leak in PcaSvc)
    print "Copying manifests...\n";
    opendir OUTDIR, $out;
    foreach my $f ( readdir OUTDIR ) {
        if ( $f =~ m/\.exe$/ ) {
            DoCopy('manifest.xml', "$out/$f.manifest");
        }
    }
    closedir OUTDIR;
}
else {
    # Just use /usr/bin/perl
    foreach my $f ( '/usr/bin/perl' ) {
        DoCopy($f, $out);
        chmod 0755, "$out/".basename($f)
    }

    print "Not bundling wxWidgets...\n";
    print "Not bundling VC10 runtime...\n";
    print "Not including manifests...\n";
}

print "Done\n";

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
    DoCopy($src, $dest);
}

sub DoCopy {
    my ($src,$dest) = @_;
    $dest .= '/'.basename($src) if -d $dest or $dest =~ m/[\/\\]$/;
    mkpath(dirname($dest));
    unlink $dest if -e $dest;
    copy($src, $dest) or die "Can't copy $src to $dest: $!";
}

#copy('c:\strawberry\perl\bin\perl.exe', '.');
#copy('c:\strawberry\perl\bin\wperl.exe', '.');
