#!/bin/sh
#
# buildwin32-from-template.sh - Build distclient for Win32
#
# It's like build-dist.pl, but uses precomputed Perl dependencies

BIN=distclient-template-win32-20120309.tar.xz
URL=https://dl.dropbox.com/s/sylt8mzjp2vlzui/$BIN
DISTDIR=out-MSWin32

set -e

# Build wrapper program (using MinGW in Wine)
(cd wrapper; sh make-wine.sh || true; [ -f distclient.exe ])
# Download template (contains distclient binaries, particularly Perl)
[ -f $BIN ] || wget $URL

# Extract template
mkdir -p $DISTDIR
cd $DISTDIR
tar xf ../$BIN
# Update template with just-built binaries from the source tree
for x in *; do
    [ -d $x ] && continue
    for y in ../wrapper ..; do # ../..
        if [ -f $y/$x ]; then
            echo Updating $x with $y/$x...
            cp -p $y/$x $x
            break
        fi
    done
done
cd ..

# Build installer
./makensis.sh
