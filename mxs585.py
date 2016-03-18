#!/usr/bin/env python

import maxpython

test1 = maxpython.MaxScaleTest("mxs585")

for i in range(0,100):
    if i % 10 == 0:
        print(str(i))
    test1.rwsplit.query_and_close("select 1")
    test1.rcmaster.query_and_close("select 1")
    test1.rcslave.query_and_close("select 1")
