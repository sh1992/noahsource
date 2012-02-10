#!/bin/bash
#
# build-tgz.sh - Assemble distribution of distclient without Perl.
#
# Used for Linux systems that have Perl already, especially EC2.
#
ARCHIVE=distclient.tar
FLAGS="--show-stored-names --owner=0 --group=0"
rm -f "$ARCHIVE" "$ARCHIVE".gz
tar --transform 's|^|distclient/|' $FLAGS \
    -cvf "$ARCHIVE" distclient.pl distclientcli.pl server.conf
LIBDIR=`perl -le 'for(@INC,"./lib"){print if -f "\$_/HTTP/Async.pm"}'`
[ -z "$LIBDIR" ] && echo Can not find HTTP::Async && exit 1
tar --transform "s|^$LIBDIR|distclient/lib|" $FLAGS \
    --append -vf "$ARCHIVE" $LIBDIR/HTTP/Async{.pm,}
gzip -9 "$ARCHIVE"
echo Created "$ARCHIVE".gz
