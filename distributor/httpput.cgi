#!/bin/sh
#
# httpput.cgi - CGI script to handle HTTP PUT
#

# TODO: Rewrite in Perl

MAXSIZE=1048576
OWNER=1000
MODE=600
DEST=.

if [ "$REQUEST_METHOD" != "PUT" ]; then
    ERROR="405 Method Not Allowed"
    echo Allow: PUT
elif [ "$HTTP_CONTENT_LENGTH" -lt 0 ] ||
     [ "$HTTP_CONTENT_LENGTH" -gt $MAXSIZE ]; then
    ERROR="413 Request Entity Too Large"
else
    UPLOAD=`mktemp -p "$DEST"`
    # TODO: Content-MD5
    if [ "$HTTP_CONTENT_ENCODING" = "gzip" ]; then
        head -c $HTTP_CONTENT_LENGTH | gzip -d > $UPLOAD || UPLOAD=
    else
        head -c $HTTP_CONTENT_LENGTH > $UPLOAD || UPLOAD=
    fi
    if [ -n "$UPLOAD" ]; then
        (chmod $MODE $UPLOAD; chown $OWNER $UPLOAD) >/dev/null 2>&1
    else
        ERROR="400 Bad Request"
    fi
fi
echo Content-type: text/plain
if [ -n "$ERROR" ]; then
    echo Status: $ERROR
    echo
    echo Your request could not be processed. $ERROR.
elif [ -n "$UPLOAD" ]; then
    echo
    echo $UPLOAD
else
    echo Status: 500 Internal Server Error
    echo
    echo Your request could not be processed. Unknown error.
fi
