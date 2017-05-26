#!/usr/bin/env python

###
## @file mxs585.py Regression case for MXS-585 "Intermittent connection failure with MaxScale 1.2/1.3 using MariaDB/J 1.3"
## - open connection, execute simple query and close connection in the loop

import maxpython

test1 = maxpython.MaxScaleTest("mxs585.py")

for i in range(0,100):
    if i % 10 == 0:
        print(str(i))
    test1.maxscale['rwsplit'].query_and_close("select 1")
    test1.maxscale['rcmaster'].query_and_close("select 1")
    test1.maxscale['rcslave'].query_and_close("select 1")
