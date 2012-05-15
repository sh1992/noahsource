#!/usr/bin/python
#
# slowsquare.py - Example distributed computing distributor
#

import asyncore, asynchat, socket, hashlib, urllib
import json, random

# URL of .tar.gz file named slowsquare.app that contains slowsquare-app.pl
# as all/app.pl
# tar --transform 's|.*|all/app.pl|S' -czf slowsquare.app slowsquare-app.pl
APP_MD5 = "8e892921ccf4ab68af29d65b82a9f60e"
APP_URL = "http://dl.dropbox.com/s/sylt8mzjp2vlzui/slowsquare-20120515.app"

SERVER_HOST = "localhost"
SERVER_PORT = 9933
SERVER_PASS = "643d1e29b4c27bd729faa938ea99e604"


class distributor(asynchat.async_chat):
    def __init__(self, host, port, key, name):
        # Connect to the server
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
        asynchat.async_chat.__init__(self, sock=sock)
        self.ibuffer = []
        self.set_terminator("\n")
        self.key = key      # distserver password
        self.name = name    # Workunit name prefix
        self.index = 0      # Sequential workunit number
        # Workunit queues
        self.unsent = []
        self.running = {}

    def collect_incoming_data(self, data):
        self.ibuffer.append(data)

    def found_terminator(self):
        cmd = "".join(self.ibuffer).strip().split(None, 1)
        if len(cmd) < 2: cmd.append(None)
        cmd[0] = cmd[0].upper()
        self.ibuffer = []
        # Handle commands received from server
        if cmd[0] == "ERR":
            raise Exception, "ERR %s"%cmd[1]
        elif cmd[0] == "HELLO":
            self.push('HELLO 0 %s %s\n'%(self.name, self.key))
            self.check_for_work()
        elif cmd[0] == "NEWWORKER":
            self.check_for_work() # Try to send work
        elif cmd[0] == "NOWORKERS":
            pass # Do nothing
        elif cmd[0] == "OK":
            pass # Do nothing
        elif cmd[0] == "WORKER":
            self.send_work(json.loads(cmd[1])) # Send this worker some work
        elif cmd[0] == "WORKACCEPTED":
            pass # Do nothing
        elif cmd[0] == "WORKFINISHED" or cmd[0] == "WORKFAILED" or \
             cmd[0] == "WORKREJECTED":
            obj = json.loads(cmd[1]);
            name = obj['id']
            failed = False
            if name not in self.running:
                print "%s: %s, not running" % (cmd[0], name)
                # Got response for unknown workunit, ignore
            elif cmd[0] == "WORKFINISHED":
                # Display result
                files = obj['files']
                if len(files) != 1:
                    print "Wrong number of files"
                    failed = True
                elif files[0][2].startswith('data:'):
                    try:
                        answer = int(urllib.urlopen(files[0][2]).read())
                    except:
                        print "Failed to decode answer"
                        failed = True
                    finally:
                        print "%s: %s, %d^2 = %s" % (cmd[0], name,
                                                     self.running[name], answer)
                else:
                    print "Unsupported file location"
                    failed = True
            else: failed = True
            if failed:
                print "%s: %s, %d^2 = ?" % (cmd[0], name, self.running[name])
                if 'error' in obj and obj['error']:
                    print "Error: %s" % obj['error']
                # Mark item as incomplete, resend it later
                self.unsent.append(self.running[name])
                del(self.running[name])
            self.check_for_work()
        else:
            raise Exception, "Unknown command %s"%cmd[0]

    def check_for_work(self):
        # Generate work, if necessary
        while len(self.unsent) <= 0:
            self.unsent.append(random.randrange(1,20))

        # Check if we have work to send, and if so, request a worker
        if len(self.unsent) > 0:
            self.push('HAVEWORK\n')

    def send_work(self, worker):
        # Ensure there are unsent items to send
        if len(self.unsent) <= 0: return
        # Create workunit name
        self.index += 1
        name = "%s-%05d" % (self.name, self.index)
        # Pop item from unsent queue and move it to running list
        item = self.unsent.pop()
        self.running[name] = item
        # Encode input file
        item_str = str(item)
        item_md5 = hashlib.md5(item_str).hexdigest()
        item_url = "data:text/plain,%s"%item_str
        # Create object
        obj = {'id': name, 'duration': item+1,
               'files': [[APP_MD5, APP_URL, "slowsquare.app"],
                         [item_md5, item_url, "temp/%s"%name]],
               'upload': 'data:', 'worker': worker['id']}
        # Send to worker
        print "Sending %s to %s as %s" % (item_str, worker['name'], name)
        self.push('DISPATCH %s\n' % json.dumps(obj))
        # See if we have more work to send
        self.check_for_work()

process = distributor(SERVER_HOST, SERVER_PORT, SERVER_PASS, "slowsquare")
asyncore.loop()
