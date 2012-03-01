#!/bin/sh
#
# makensis.sh - Build NSIS installers, setting variables appropriately
#

set -e

FLAGS=$(
    # Distclient version number
    echo -n '-DVERSION='
    perl -nle 'print($1),last if m/^our \$VERSION = (\d+)/' distclient.pl
    # Extract client configuration directives
    perl -MJSON -nle '$x=from_json($_);while(($k,$v)=each %$x){print "\"-DDISTCLIENT_",uc $k,"=$v\""}' < server.conf
)

for NSI in fetchdist.nsi distclient.nsi; do
  SHORTNAME=`basename "$NSI" .nsi`
  EXE="$SHORTNAME.exe"
  # Pipe into a new shell because the quoting situation in variables
  # (e.g. $FLAGS) is an absolute mess.
  echo makensis $FLAGS "\"-DSHORTNAME=$SHORTNAME\"" "\"$NSI\"" | sh -x
done
