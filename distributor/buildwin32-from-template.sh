#!/bin/sh
#
# buildwin32-from-template.sh - Build distclient for Win32
#
# It's like buildwin32.pl, but doesn't recompute the Perl dependencies

BIN=distclient-template-win32-20110506.tar.xz
URL=http://students.ncf.edu/noah.anderson/spec/$BIN

set -e

# Build ga-spectroscopy-client and perl-wrapper for Windows
# (using MinGW in Wine)
(cd ..; sh make-wine.sh)

# Download template (contains distclient binaries, particularly Perl)
[ -f $BIN ] || wget $URL

# Extract template
mkdir -p out
cd out
tar xf ../$BIN
# Update template with just-build binaries from the source tree
for x in *; do
  [ -d $x ] && continue
  for y in ../wrapper ../.. ..; do
    if [ -f $y/$x ]; then
      echo Updating $x with $y/$x...
      cp -p $y/$x $x
      break
    fi
  done
done
cd ..

# Build installer
makensis distclient.nsi


