#!/usr/bin/python
#
# slowsquare-app.py - Slowly square two numbers (Python version)
#

import sys, os, time, tempfile

if len(sys.argv) != 3:
    print "Usage: app.py something.app input-file"
    sys.exit(1)

# Read the second input file (the first input file is the app)
infile = sys.argv[2]
fh = open(infile, 'r')
# Read first line from infile, convert to number
number = int(fh.readline())
fh.close()

# Create output file
fh = tempfile.NamedTemporaryFile(prefix='temp-output-', dir='.', delete=False)
outfile = fh.name
# Note that NamedTemporaryFile automatically opens the file for us

# Update progress
print "DISTCLIENT STATUS 0/%d" % (number)
# Square value
output = 0
for i in range(number):
    # Do something
    output += number
    time.sleep(1)
    # Update progress
    print "DISTCLIENT STATUS %d" % (i+1)

# We don't need the input file anymore
os.unlink(infile)

# Write a result to the output file and return it
fh.write(str(output))
fh.close()
print "DISTCLIENT OUTPUT %s" % (outfile)

