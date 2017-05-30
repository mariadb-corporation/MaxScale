#!/usr/bin/env python

import maxpython

test = maxpython.MaxScaleTest("nsfilter")

server_id = []

for conn in test.repl:
    server_id[conn] = conn.query("SELECT @@server_id")

nomatch = test.maxscale['rwsplit'].query("SELECT @@server_id")
match = test.maxscale['rwsplit'].query("SELECT \"test\", @@server_id")

print(nomatch)
print(match)
