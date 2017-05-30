#!/bin/python3

import http.client
import os
import json

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

for i in entry_points:
    print("Testing", i)
    data = ""
    try:
        conn = http.client.HTTPConnection(os.getenv("maxscale_IP"), 8080)
        conn.request("GET", i)
        req = conn.getresponse()
        data = req.read().decode('ascii')
        print(json.loads(data))
    except Exception as ex:
        print("Exception (", ex, "):", data)
        exit(1)
