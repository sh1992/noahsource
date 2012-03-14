#!/usr/bin/perl
#
# monitor.cgi - Produce server-side events for status panel
#

use IO::Socket::INET;
use warnings;
use strict;

$|=1;                   # Turn on autoflush

my $sock = IO::Socket::INET->new('localhost:9933') or
    err("Cannot access server: $!");
my $welcome = <$sock>;
err('Server is not welcoming') unless $welcome =~ m/^HELLO/;
print $sock "MONITOR\n";

print "Content-type: text/event-stream\nCache-Control: no-cache\n\n";

print 'data: DATE ', time, "\n\n";

my $i = 0;
while (<$sock>) {
    s/[\r\n]//g;
    print "data: $_\n\n";
    if ( $i++ >= 100 ) {
        print 'data: DATE ', time, "\n\n";
        $i = 0;
    }
}

sub err {
    print join("\n",'Status: 503 Temporarily unavailable',
            'Content-type: text/plain', 'Cache-Control: no-cache', '', @_, '');
    exit 1;
}

