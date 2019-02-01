#!/usr/bin/env python

# Copyright (c) 2016 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2022-01-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.

import json
import sys
import argparse

parser = argparse.ArgumentParser(description = """

This program generates a schema file for a single table by reading a tab
separated list of field and type names from the standard input.

To use this script, pipe the output of the `mysql` command line into the
`cdc_one_schema.py` script:

  mysql -ss -u <user> -p -h <host> -P <port> -e 'DESCRIBE `<database>`.`<table>`'|./cdc_one_schema.py <database> <table>

Replace the <user>, <host>, <port>, <database> and <table> with appropriate
values and run the command. Note that the `-ss` parameter is mandatory as that
will generate the tab separated output instead of the default pretty-printed
output.

An .avsc file named after the database and table name will be generated in the
current working directory. Copy this file to the location pointed by the
`avrodir` parameter of the avrorouter.

Alternatively, you can also copy the output of the `mysql` command to a file and
feed it into the script if you cannot execute the SQL command directly:

  # On the database server
  mysql -ss -u <user> -p -h <host> -P <port> -e 'DESCRIBE `<database>`.`<table>`' > schema.tsv
  # On the MaxScale server
  ./cdc_one_schema.py <database> <table> < schema.tsv

""", formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("DATABASE", help="The database name where the table is located")
parser.add_argument("TABLE", help="The name of the table")

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

    type = "string"

    if name in ("date", "datetime", "time", "timestamp", "year", "tinytext", "text",
	        "mediumtext", "longtext", "char", "varchar", "enum", "set"):
        type = "string"
    elif name in ("tinyblob", "blob", "mediumblob", "longblob", "binary", "varbinary"):
        type = "bytes"
    elif name in ("int", "smallint", "mediumint", "integer", "tinyint", "short", "bit"):
        type = "int"
    elif name in ("float"):
        type = "float"
    elif name in ("double", "decimal"):
        type = "double"
    elif name in ("long", "bigint"):
        type = "long"

    res["type"] = ["null", type]

    res["name"] = row[0].lower()

    return res

try:
    schema = dict(namespace="MaxScaleChangeDataSchema.avro", type="record", name="ChangeRecord", fields=[])
    for line in sys.stdin:
        schema["fields"].append(parse_field(line.split('\t')))

    print("Creating: {}.{}.000001.avsc".format(opts.DATABASE, opts.TABLE))
    dest = open("{}.{}.000001.avsc".format(opts.DATABASE, opts.TABLE), 'w')
    dest.write(json.dumps(schema))
    dest.close()

except Exception as e:
    print(e)
    exit(1)

