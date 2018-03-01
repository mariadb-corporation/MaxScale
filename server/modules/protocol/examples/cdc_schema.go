// Copyright (c) 2016 MariaDB Corporation Ab
//
// Use of this software is governed by the Business Source License included
// in the LICENSE.TXT file and at www.mariadb.com/bsl11.
//
// Change Date: 2019-07-01
//
// On the date above, in accordance with the Business Source License, use
// of this software will be governed by version 2 or later of the General
// Public License.

package main

import (
	"database/sql"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"os"
	"regexp"
	"strconv"
)

import _ "github.com/go-sql-driver/mysql"

var host = flag.String("host", "localhost", "Server address")
var port = flag.Int("port", 3306, "Server port")
var user = flag.String("user", "", "Server user")
var passwd = flag.String("password", "", "Server password")
var debug = flag.Bool("debug", false, "Debug output")

func PrintUsage() {
	fmt.Println(`Usage: cdc_schema [OPTIONS]

This program generates CDC schema files for all the tables in a database. The
schema files need to be generated if the binary log files do not contain the
CREATE TABLE events that define the table layout.

The "user" and "password" flags are required.
`)

	flag.PrintDefaults()
}

// Avro field
type Field struct {
	Name     string `json:"name"`
	Type     string `json:"type"`
	RealType string `json:"real_type"`
	Length   int    `json:"length"`
}

// Avro schema
type Schema struct {
	Namespace string  `json:"namespace"`
	Type      string  `json:"type"`
	Name      string  `json:"name"`
	Fields    []Field `json:"fields"`
}

// Debugging output helper function
func LogObject(obj interface{}) {
	js, err := json.Marshal(obj)
	if err != nil {
		log.Fatal("Failed to parse object: ", err)
	} else {
		log.Println("Unsupported type: ", string(js))
	}
}

var field_re *regexp.Regexp
var length_re *regexp.Regexp

// Convert the SQL type to the appropriate Avro type
func (f *Field) ToAvroType() {
	orig := f.Type
	f.Type = field_re.ReplaceAllString(f.Type, "")
	f.Length = -1
	f.RealType = f.Type
	switch f.Type {
	case "date", "datetime", "time", "timestamp", "year", "tinytext", "text",
		"mediumtext", "longtext", "char", "varchar":
		f.Type = "string"
		f.Length, _ = strconv.Atoi(length_re.ReplaceAllString(orig, "$1"))
	case "enum", "set":
		f.Type = "string"
	case "tinyblob", "blob", "mediumblob", "longblob", "binary", "varbinary":
		f.Type = "bytes"
	case "int", "smallint", "mediumint", "integer", "tinyint", "short", "bit":
		f.Type = "int"
	case "float":
		f.Type = "float"
	case "double", "decimal":
		f.Type = "double"
	case "null":
		f.Type = "null"
	case "long", "bigint":
		f.Type = "long"
	default:
		LogObject(f)
		f.Type = "string"
	}
}

// Create and store the Avro schema to disk
func StoreSchema(db *sql.DB, schema, table string) {
	file, err := os.Create(schema + "." + table + ".000001.avsc")
	if err != nil {
		log.Fatal("Failed to create file:", err)
	}
	defer file.Close()

	encoder := json.NewEncoder(file)
	fields, err := db.Query("DESCRIBE " + schema + "." + table)
	if err != nil {
		log.Fatal("Failed to query for description of "+schema+"."+table+": ", err)
	}
	defer fields.Close()

	fieldlist := make([]Field, 0, 10)

	for fields.Next() {
		var field Field
		var Null, Key, Default, Extra string

		fields.Scan(&field.Name, &field.Type, &Null, &Key, &Default, &Extra)
		field.ToAvroType()
		fieldlist = append(fieldlist, field)
	}

	encoder.Encode(Schema{Namespace: "MaxScaleChangeDataSchema.avro", Type: "record", Name: "ChangeRecord", Fields: fieldlist})
}

// Main funtion that queries the database for table names
func main() {
	var err error
	field_re, err = regexp.Compile("[(].*")

	if err != nil {
		log.Fatal("Error: ", err)
	}

	length_re, err = regexp.Compile(".*[(](.*)[)].*")

	if err != nil {
		log.Fatal("Error: ", err)
	}

	flag.Usage = PrintUsage
	flag.Parse()

	if len(*user) == 0 || len(*passwd) == 0 {
		log.Fatal("Both the -user and -password flags are mandatory. See output of -help for more details.")
	}

	var connect_str string = *user + ":" + *passwd + "@tcp(" + *host + ":" + strconv.Itoa(*port) + ")/"

	if *debug {
		log.Println("Connect string: ", connect_str)
	}

	db, err := sql.Open("mysql", connect_str)
	if err != nil {
		log.Fatal("Failed to open connection to", *host, *port, ":", err)
	}
	defer db.Close()

	databases, err := db.Query("SHOW DATABASES")
	if err != nil {
		log.Fatal("Failed to query for databases: ", err)
	}
	defer databases.Close()

	for databases.Next() {
		var schemaname string
		databases.Scan(&schemaname)

		// Skip the system databases
		switch schemaname {
		case "mysql", "information_schema", "performance_schema":
			continue
		}

		tables, err := db.Query("SHOW TABLES FROM " + schemaname)
		if err != nil {
			log.Fatal("Failed to query for tables from "+schemaname+": ", err)
		}
		defer tables.Close()

		for tables.Next() {
			var tablename string
			tables.Scan(&tablename)

			if *debug {
				log.Println("Processing", schemaname, ".", tablename)
			}

			StoreSchema(db, schemaname, tablename)
		}
	}
}
