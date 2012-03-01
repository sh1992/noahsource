#!/bin/bash
#
# build-tgz.sh - Assemble distribution of xmpp-client.
#

ARCHIVE=xmpp-tunnel.tar
FLAGS="--show-stored-names --owner=0 --group=0"
rm -f "$ARCHIVE" "$ARCHIVE".gz
tar --transform 's|^|xmpp-tunnel/|' $FLAGS \
    -cvf "$ARCHIVE" xmpp-tunnel xmpp-tunnel-watchdog xmpp-tunnel-wait \
                    client.conf id_rsa.pub
gzip -9 "$ARCHIVE"
echo Created "$ARCHIVE".gz

cp -p "$ARCHIVE".gz ~/Dropbox/spec/ && echo Copied "$ARCHIVE".gz to Dropbox.
