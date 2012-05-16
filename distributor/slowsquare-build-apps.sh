#!/bin/bash
#
# slowsquare-build-apps.sh - Build .app files for slowsquare
#

echo "Building apps"

# slowsquare.app (simple Perl version)
TD=`mktemp -d`
mkdir "$TD/all"
cp -p slowsquare-app.pl "$TD/all/app.pl"
(cd $TD; tar czf - .) > slowsquare.app
md5sum slowsquare.app | cut -c1-32 > slowsquare.app.md5
rm -r $TD

# slowsquare-python.app (Python version)
TD=`mktemp -d`
mkdir "$TD/all"
cp -p python-app.pl "$TD/all/app.pl"
cp -p slowsquare-app.py "$TD/all/app.py"
(cd $TD; tar czf - .) > slowsquare-python.app
md5sum slowsquare-python.app | cut -c1-32 > slowsquare-python.app.md5
rm -r $TD

TARGET=../temp
[ `hostname` = 'gaspec-chemlab' ] && TARGET=~/spec/temp
echo "Copying apps to $TARGET for access via http://localhost:9990/spec/temp/"
cp -p slowsquare.app slowsquare-python.app "$TARGET"
