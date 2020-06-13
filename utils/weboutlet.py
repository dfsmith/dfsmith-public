#!/usr/bin/env python3

import sys
from bs4 import BeautifulSoup
import requests

class WebPowerOutlets:
    def __init__(self, hostname, user=None, passwd=None):
        self.baseurl = "http://" + hostname
        self.outlet = []
        self.lookup = {}
        self.auth = None

        if user:
            self.auth = requests.auth.HTTPBasicAuth(user, passwd)
        html = self.get("index.htm")
        soup = BeautifulSoup(html.text, "html.parser")

        n = 0
        try:
            while True:
                tb = self.outlettb(soup, n)
                o = self.state(tb)
                self.outlet.append(o)
                self.lookup[o['number']] = n
                self.lookup[o['name']] = n
                n = n + 1
        except:
            pass

    def get(self, loc):
        return requests.get(self.baseurl+"/"+loc, auth=self.auth)

    def outlettb(self, soup, n):
        return soup.select("table table:nth-of-type(2) tr:nth-of-type("+str(3+n)+") td")

    def state(self, tb):
        num = tb[0].decode_contents()
        name = tb[1].decode_contents()
        state = self.is_on(tb[2].select("font")[0].decode_contents())
        return {"number": num, "on": state, "name": name}

    def is_on(self, onoff):
        if onoff in ['on', 'ON', 'powered', '1']:
            return True
        if onoff in ['off', 'OFF', 'unpowered', '0']:
            return False
        raise Exception("cannot decode on/off state '%s'" % onoff)

    def set(self, outlet, state):
        onoff = 'ON' if state else 'OFF'
        html = self.get('outlet?'+outlet+'='+onoff)
        return "ok" if html else "cannot switch '%s'" % outlet

if __name__ == "__main__":
    hostname = "garageoutlets.dfsmith.net"
    if len(sys.argv) == 2 and sys.argv[1] in [ '-?', '-h', '--help']:
        print("Syntax: %s [<n>[=<on|off>]]..." % sys.argv[0])
        print("Web Power Switch control script: see http://www.digital-loggers.com")
        print("Using hostname: %s (auth with netrc)" % hostname)
        exit(0)

    wpo = WebPowerOutlets(hostname)
    if len(sys.argv) < 2:
        # print status
        for o in wpo.outlet:
            print(o)
    else:
        for arg in sys.argv[1:]:
            if '=' in arg:
                [outlet, state] = arg.split('=', 1)
                print(wpo.set(outlet, wpo.is_on(state)))
            else:
                print(wpo.outlet[wpo.lookup[arg]]['on'])
