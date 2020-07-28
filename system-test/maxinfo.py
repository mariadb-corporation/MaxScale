#!/usr/bin/python3

import http.client
import os
import json
import subprocess
import threading

# Needs to be declared here to allow Python 3 modules to be used
def prepare_test(testname = "replication"):
    subprocess.call(os.getcwd() + "/non_native_setup " + str(testname), shell=True)

prepare_test("maxinfo.py")

# Test all Maxinfo HTTP entry points
entry_points = ["/services",
		"/listeners",
		"/modules",
		"/monitors",
		"/sessions",
		"/clients",
		"/servers",
		"/variables",
		"/status",
		"/event/times"]

decoder = json.JSONDecoder()

def test_thr(thrnum):
    for r in range(0,10):
        for i in entry_points:
            data = ""
            try:
                conn = http.client.HTTPConnection(os.getenv("maxscale_network"), 8080)
                conn.request("GET", i)
                req = conn.getresponse()
                data = req.read().decode('ascii')
                json.loads(data)
            except Exception as ex:
                print("Thread", thrnum, "Exception (", ex, "):", data)
                exit(1)

thr = []

for i in range(0, 10):
    thr.append(threading.Thread(target=test_thr, args=(i,)))

print("Created", len(thr), "threads")

for i in thr:
    i.start()

print("Started threads")

for i in thr:
    i.join()
