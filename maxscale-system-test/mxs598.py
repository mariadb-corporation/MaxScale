#!/usr/bin/env python


###
## @file mxs598.py Regression case for MXS-598 "SSL RW Router / JDBC Exception"
## - use SSL for Maxscale client connection
## - simple transactions in the loop

import maxpython

test1 = maxpython.MaxScaleTest("mxs598.py")

print("Connecting to MaxScale")
for i in test1.maxscale.values():
    i.connect("useSSL=true&requireSSL=true&verifyServerCert=false")

print("Trying 100 simple transactions on all services")
for i in range(0,100):
    for x in test1.maxscale.values():
        x.begin()
        x.query("insert into test.t1 values (1)")
        x.query("select * from test.t1")
        x.commit()

print("Closing connections")
for i in test1.maxscale.values():
    i.disconnect()
