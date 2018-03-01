#!/usr/bin/env python

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

#
# This program requires the MySQL Connector/Python to work
#

import mysql.connector as mysql
import json
import sys
import argparse

parser = argparse.ArgumentParser(description = "CDC Schema Generator", conflict_handler="resolve", epilog="""This program generates CDC schema files for a specific table or all the tables in a database. The
schema files need to be generated if the binary log files do not contain the
CREATE TABLE events that define the table layout.""")
parser.add_argument("-h", "--host", dest="host", help="Network address where the connection is made", default="localhost")
parser.add_argument("-P", "--port", dest="port", help="Port where the connection is made", default="3306")
parser.add_argument("-u", "--user", dest="user", help="Username used when connecting", default="")
parser.add_argument("-p", "--password", dest="password", help="Password used when connecting", default="")
parser.add_argument("DATABASE", help="Generate Avro schemas for this database")

opts = parser.parse_args(sys.argv[1:])

def parse_field(row):
    res = dict()
    parts = row[1].lower().split('(')
    name = parts[0]

    res["real_type"] = name

    if len(parts) > 1 and name not in ["enum", "set", "decimal"]:
        res["length"] = int(parts[1].split(')')[0])
    else:
        res["length"] = -1

    if name in ("date", "datetime", "time", "timestamp", "year", "tinytext", "text",
	        "mediumtext", "longtext", "char", "varchar", "enum", "set"):
        res["type"] = "string"
    elif name in ("tinyblob", "blob", "mediumblob", "longblob", "binary", "varbinary"):
        res["type"] = "bytes"
    elif name in ("int", "smallint", "mediumint", "integer", "tinyint", "short", "bit"):
        res["type"] = "int"
    elif name in ("float"):
        res["type"] = "float"
    elif name in ("double", "decimal"):
        res["type"] = "double"
    elif name in ("null"):
        res["type"] = "null"
    elif name in ("long", "bigint"):
        res["type"] = "long"
    else:
        res["type"] = "string"


    res["name"] = row[0].lower()

    return res

try:
    conn = mysql.connect(user=opts.user, password=opts.password, host=opts.host, port=opts.port)
    cursor = conn.cursor()
    cursor.execute("SHOW TABLES FROM {}".format(opts.DATABASE))

    tables = []
    for res in cursor:
        tables.append(res[0])


    for t in tables:
        schema = dict(namespace="MaxScaleChangeDataSchema.avro", type="record", name="ChangeRecord", fields=[])
        cursor.execute("DESCRIBE {}.{}".format(opts.DATABASE, t))

        for res in cursor:
            schema["fields"].append(parse_field(res))

        dest = open("{}.{}.000001.avsc".format(opts.DATABASE, t), 'w')
        dest.write(json.dumps(schema))
        dest.close()

    cursor.close()
    conn.close()

except Exception as e:
    print(e)
    exit(1)

