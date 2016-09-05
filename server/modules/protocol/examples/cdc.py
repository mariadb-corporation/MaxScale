#!/usr/bin/env python3

# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl.
#
# Change Date: 2019-01-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.

import time
import json
import re
import sys
import socket
import hashlib
import argparse
import subprocess
import selectors
import binascii
import os

# Read data as JSON
def read_json():
    decoder = json.JSONDecoder()
    rbuf = bytes()
    ep = selectors.EpollSelector()
    ep.register(sock, selectors.EVENT_READ)

    while True:
        pollrc = ep.select(timeout=int(opts.read_timeout) if int(opts.read_timeout) > 0 else None)
        try:
            buf = sock.recv(4096, socket.MSG_DONTWAIT)
            rbuf += buf
            while True:
                rbuf = rbuf.lstrip()
                data = decoder.raw_decode(rbuf.decode('ascii'))
                rbuf = rbuf[data[1]:]
                print(json.dumps(data[0]))
        except ValueError as err:
            sys.stdout.flush()
            pass
        except Exception:
            break

# Read data as Avro
def read_avro():
    ep = selectors.EpollSelector()
    ep.register(sock, selectors.EVENT_READ)

    while True:
        pollrc = ep.select(timeout=int(opts.read_timeout) if int(opts.read_timeout) > 0 else None)
        try:
            buf = sock.recv(4096, socket.MSG_DONTWAIT)
            os.write(sys.stdout.fileno(), buf)
            sys.stdout.flush()
        except Exception:
            break

parser = argparse.ArgumentParser(description = "CDC Binary consumer", conflict_handler="resolve")
parser.add_argument("-h", "--host", dest="host", help="Network address where the connection is made", default="localhost")
parser.add_argument("-P", "--port", dest="port", help="Port where the connection is made", default="4001")
parser.add_argument("-u", "--user", dest="user", help="Username used when connecting", default="")
parser.add_argument("-p", "--password", dest="password", help="Password used when connecting", default="")
parser.add_argument("-f", "--format", dest="format", help="Data transmission format", default="JSON", choices=["AVRO", "JSON"])
parser.add_argument("-t", "--timeout", dest="read_timeout", help="Read timeout", default=0)
parser.add_argument("FILE", help="Requested table name in the following format: DATABASE.TABLE[.VERSION]")
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

# Request a data stream
sock.send(bytes(("REQUEST-DATA " + opts.FILE + (" " + opts.GTID if opts.GTID else "")).encode()))

if opts.format == "JSON":
    read_json()
elif opts.format == "AVRO":
    read_avro()
