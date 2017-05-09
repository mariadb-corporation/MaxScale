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

import socket
import hashlib
import argparse
import binascii
import sys

parser = argparse.ArgumentParser(description = "CDC Binary consumer", conflict_handler="resolve")
parser.add_argument("-h", "--host", dest="host", help="Network address where the connection is made", default="localhost")
parser.add_argument("-P", "--port", dest="port", help="Port where the connection is made", default="4001")
parser.add_argument("-u", "--user", dest="user", help="Username used when connecting", default="")
parser.add_argument("-p", "--password", dest="password", help="Password used when connecting", default="")
parser.add_argument("-f", "--format", dest="format", help="Data transmission format", default="JSON", choices=["AVRO", "JSON"])
parser.add_argument("-t", "--timeout", dest="read_timeout", help="Read timeout", default=0)
parser.add_argument("GTID", help="Requested GTID position", default=None, nargs='?')

opts = parser.parse_args(sys.argv[1:])

sock = socket.create_connection([opts.host, opts.port])

# Authentication
auth_string = binascii.b2a_hex((opts.user + ":").encode())
auth_string += bytes(hashlib.sha1(opts.password.encode("utf_8")).hexdigest().encode())
sock.send(auth_string)

# Discard the response
response = str(sock.recv(1024)).encode('utf_8')

# Register as a client as request Avro format data
sock.send(bytes(("REGISTER UUID=XXX-YYY_YYY, TYPE=" + opts.format).encode()))

# Discard the response again
response = str(sock.recv(1024)).encode('utf_8')

# Register as a client as request Avro format data

if opts.GTID:
    sock.send(bytes(("QUERY-TRANSACTION " + opts.GTID).encode()))
else:
    sock.send(bytes("QUERY-LAST-TRANSACTION".encode()))

response = sock.recv(1024)
print(response.decode('utf_8'))
