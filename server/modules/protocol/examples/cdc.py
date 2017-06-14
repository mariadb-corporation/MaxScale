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

import time
import sys
import socket
import hashlib
import argparse
import selectors
import binascii
import os

schema_read = False

def read_data():
    global schema_read
    sel = selectors.DefaultSelector()
    sel.register(sock, selectors.EVENT_READ)

    while True:
        try:
            events = sel.select(timeout=int(opts.read_timeout) if int(opts.read_timeout) > 0 else None)
            buf = sock.recv(4096, socket.MSG_DONTWAIT)
            if len(buf) > 0:
                # If the request for data is rejected, an error will be sent instead of the table schema
                if not schema_read:
                    if "err" in buf.decode().lower():
                        print(buf.decode(), file=sys.stderr)
                        exit(1)
                    else:
                        schema_read = True

                os.write(sys.stdout.fileno(), buf)
                sys.stdout.flush()

            else:
                raise Exception('Socket was closed')

        except BlockingIOError:
            break
        except Exception as ex:
            print(ex, file=sys.stderr)
            break


def check_for_err(err):
    if "err" in err.lower().strip():
        print(err.strip(), file=sys.stderr)
        exit(1)


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
response = sock.recv(1024).decode()

check_for_err(response)

# Register as a client as request Avro format data
sock.send(bytes(("REGISTER UUID=XXX-YYY_YYY, TYPE=" + opts.format).encode()))

# Discard the response again
response = sock.recv(1024).decode()

check_for_err(response)

# Request a data stream
sock.send(bytes(("REQUEST-DATA " + opts.FILE + (" " + opts.GTID if opts.GTID else "")).encode()))

read_data()
