#!/bin/bash
#
# build-tgz.sh - Assemble distribution of distclient without Perl.
#
# Used for Linux systems that have Perl already (EC2 Linux) and for systems
# that do not have Perl (EC2 FreeBSD) when used with linux-perl-*.tar.gz.
#

ARCHIVE=distclient.tar
FLAGS="--show-stored-names --owner=0 --group=0"
rm -f "$ARCHIVE" "$ARCHIVE".gz
# Package the distributed computing client
tar --transform 's|^|distclient/|S' $FLAGS \
    -cvf "$ARCHIVE" distclient.pl distclientcli.pl server.conf
# Package selected Perl Modules
LIBDIR=`perl -le 'for(@INC,"./lib"){print if -f "\$_/HTTP/Async.pm"}'`
[ -z "$LIBDIR" ] && echo Can not find HTTP::Async && exit 1
tar --transform "s|^$LIBDIR|distclient/lib|S" $FLAGS \
    --append -vf "$ARCHIVE" $LIBDIR/HTTP/Async{.pm,}
# Compress the package
gzip -9 "$ARCHIVE"
echo Created "$ARCHIVE".gz
