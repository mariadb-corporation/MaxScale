#!/usr/bin/env python

import maxpython

test1 = maxpython.MaxScaleTest("mxs598")

for i in test1.maxscale.values():
    i.connect()

for i in range(0,100):
    for x in test1.maxscale.values():
        x.begin()
        x.query("insert into test.t1 values (1)")
        x.query("select * from test.t1")
        x.commit()

for i in test1.maxscale.values():
    i.disconnect()
