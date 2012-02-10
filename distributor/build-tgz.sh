#!/bin/sh
#
# build-tgz.sh - Assemble distribution of distclient without Perl.
#
# Used for Linux systems that have Perl already, especially EC2.
#
ARCHIVE=distclient.tar.gz
tar --transform 's|^|distclient/|' --show-stored-names --owner=0 --group=0 \
    -cvzf "$ARCHIVE" distclient.pl \
    distclientcli.pl server.conf lib
echo Created $ARCHIVE
