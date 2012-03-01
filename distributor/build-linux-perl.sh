#!/bin/sh
#
# build-linux-perl.sh - Build a distributution of Linux Perl. Normally this is
#     unnecessary since most systems have Perl, but some use unthreaded Perl.
#

ARCH=`uname -m`
if [ "$ARCH" = "x86_64" -o "$ARCH" = "amd64" ]; then ARCH=x64
else ARCH=x86; fi

./build-dist.pl
cd out-linux
tar czf ../linux-perl-$ARCH.tar.gz perl lib
