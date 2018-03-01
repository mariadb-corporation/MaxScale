#!/usr/bin/env python3

# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2019-07-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.

import sys, binascii, hashlib, argparse

parser = argparse.ArgumentParser(description = "CDC User manager", epilog = "Append the output of this program to /var/lib/maxscale/<service name>/cdcusers")
parser.add_argument("USER", help="Username")
parser.add_argument("PASSWORD", help="Password")
opts = parser.parse_args(sys.argv[1:])

print((opts.USER + ":") + hashlib.sha1(hashlib.sha1(opts.PASSWORD.encode()).digest()).hexdigest().upper())
