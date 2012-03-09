#!/bin/bash
#
# build-app.sh - Build ga-spectroscopy.app file for distributed ga-spectroscopy
#

set -e

# Download spcat binaries for {windows,linux}-{x86,x86_64}
SPCATBIN=spcat-20111028.tar.gz
SPCATURL=https://dl.dropbox.com/s/sylt8mzjp2vlzui/$SPCATBIN
[ -f $SPCATBIN ] || wget $SPCATURL

# Rebuild ga-spectroscopy-client (Windows build conducted using MinGW in Wine)
echo Rebuilding ga-spectroscopy-client...
(
    cd ..
    rm -f ga-spectroscopy-client{,.exe}
    make
    sh make-wine.sh || true             # Crashes sometimes
    [ -f ga-spectroscopy-client.exe ]
)

# Remove old app directory tree
if [ -d app ]; then
    cd app
    rm -rf win32 win64 linux-x86 linux-x86_64 all any
    cd ..
    rmdir app
fi

# Create new directory tree
mkdir -p app/{all,win{32,64},linux-{x86,x86_64}}
cd app

# Manifest files (For all editions of Windows)
for x in win32 win64; do
    cp -p ../manifest.xml $x/ga-spectroscopy-client.exe.manifest
    cp -p ../manifest.xml $x/spcat.exe.manifest
done
cp -p ../../ga-spectroscopy-client.exe win32        # 32-bit Windows
cp -p ../../ga-spectroscopy-client.exe win64        # 64-bit Windows (FIXME)
cp -p ../../ga-spectroscopy-client linux-x86        # 32-bit Linux
cp -p ../../ga-spectroscopy-client linux-x86_64     # 64-bit Linux (FIXME)
tar xzf ../$SPCATBIN                                # SPCAT (all versions)
cp -p ../gaspecclient.pl all/app.pl                 # Perl code (all versions)

# Create application package
tar cf - . | gzip -9 > ../ga-spectroscopy.app
echo Packaging complete
