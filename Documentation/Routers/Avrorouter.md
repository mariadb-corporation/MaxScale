# Avrorouter

The avrorouter is a MariaDB 10.0 binary log to Avro file converter. It consumes
binary logs from a local directory and transforms them into a set of Avro files.
These files can then be queried by clients for various purposes.

This router is intended to be used in tandem with the [Binlog Server](Binlogrouter.md).
The Binlog Server can connect to a master server and request binlog records. These
records can then consumed by the avrorouter directly from the binlog cache of
the Binlog Server. This allows MariaDB MaxScale to automatically transform binlog events
on the master to local Avro format files.

![Binlog-Avro Translator](images/Binlog-Avro.png)

The avrorouter can also consume binary logs straight from the master. This will
remove the need to configure the Binlog Server but it will increase the disk space
requirement on the master server by at least a factor of two.

The converted Avro files can be requested with the CDC protocol. This protocol
should be used to communicate with the avrorouter and currently it is the only
supported protocol. The clients can request either Avro or JSON format data
streams from a database table.

# Configuration

For information about common service parameters, refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).

## Router Parameters

### `source`

The source for the binary logs. This is an optional parameter.

The value of this parameter should be the name of a Binlog Server service.
The _filestem_ and _binlogdir_ parameters of this service will be read from
the router_options and they will be used as the source for the binary logs. This
removes the need to manually enter the right _binlogdir_ and _filestem_ options
for the avrorouter.

Here is an example of two services. The first service (`replication-router`) is
responsible for downloading the binary logs from the master and the second service
(`avro-router`) will convert the binary logs into Avro format files and store them
in `/var/lib/mysql`.

```
[replication-router]
type=service
router=binlogrouter
router_options=server-id=4000,binlogdir=/var/lib/mysql,filestem=binlog
user=maxuser
passwd=maxpwd

[avro-router]
type=service
router=avrorouter
source=replication-router
```

### `codec`

The compression codec to use. By default, the avrorouter does not use compression.

This parameter takes one of the following two values; _null_ or
_deflate_. These are the mandatory compression algorithms required by the
Avro specification. For more information about the compression types,
refer to the [Avro specification](https://avro.apache.org/docs/current/spec.html#Required+Codecs).

**Note:** Since the 2.1 version of MaxScale, all of the router options can also
be defined as parameters.

```
[replication-router]
type=service
router=binlogrouter
router_options=server-id=4000,binlogdir=/var/lib/mysql,filestem=binlog
user=maxuser
passwd=maxpwd

[avro-router]
type=service
router=avrorouter
binlogdir=/var/lib/mysql
filestem=binlog
avrodir=/var/lib/maxscale
```

## Router Options

The avrorouter is configured with a comma-separated list of key-value pairs.
Currently the router has one mandatory parameter, `binlogdir`. If no `source`
parameter is defined, binlogdir needs to be manually defined in the router
options. The following options should be given as a value to the
`router_options` parameter.

### General Options

These options control various file locations and names.

#### `binlogdir`

The location of the binary log files. This is the first mandatory parameter
and it defines where the module will read binlog files from. Read access to
this directory is required.

If used in conjunction with the Binlog Server, the value of this option should
be the same for both the Binlog Server and the avrorouter if the `source` parameter
is not used.

#### `avrodir`

The location where the Avro files are stored. This is the second mandatory
parameter and it governs where the converted files are stored. This directory
will be used to store the Avro files, plain-text Avro schemas and other files
needed by the avrorouter. The user running MariaDB MaxScale will need both read and
write access to this directory.

The avrorouter will also use the _avrodir_ to store various internal
files. These files are named _avro.index_ and _avro-conversion.ini_. By default,
the default data directory, _/var/lib/maxscale/_, is used. Before version 2.1 of
MaxScale, the value of _binlogdir_ was used as the default value for _avrodir_.

#### `filestem`

The base name of the binlog files. The default value is "mysql-bin". The binlog
files are assumed to follow the naming schema _<filestem>.<N>_ where _<N>_ is
the binlog number and _<filestem>_ is the value of this router option.

For example, with the following router option:

```
filestem=mybin,binlogdir=/var/lib/mysql/binlogs/
```

The first binlog file the avrorouter would look for is `/var/lib/mysql/binlogs/mybin.000001`.

#### `start_index`

The starting index number of the binlog file. The default value is 1.
For the binlog _mysql-bin.000001_ the index would be 1, for _mysql-bin.000005_
the index would be 5.

If you need to start from a binlog file other than 1, you need to set the value
of this option to the correct index. The avrorouter will always start from the
beginning of the binary log file.

**Note**: MaxScale version 2.2 introduces MariaDB GTID support
in Binlog Server: currently, if used with Avrorouter, the option `mariadb10_master_gtid`
must be set to off in the Binlog Server configuration in order to correclty
read the binlog files.

### Avro file options

These options control how large the Avro file data blocks can get.
Increasing or lowering the block size could have a positive effect
depending on your use case. For more information about the Avro file
format and how it organizes data, refer to the [Avro documentation](https://avro.apache.org/docs/current/).

The avrorouter will flush a block and start a new one when either `group_trx`
transactions or `group_rows` row events have been processed. Changing these
options will also allow more frequent updates to stored data but this
will cause a small increase in file size and search times.

It is highly recommended to keep the block sizes relatively large to allow
larger chunks of memory to be flushed to disk at one time. This will make
the conversion process noticeably faster.

#### `group_trx`

Controls the number of transactions that are grouped into a single Avro
data block. The default value is 1 transaction.


#### `group_rows`

Controls the number of row events that are grouped into a single Avro
data block. The default value is 1000 row events.

#### `block_size`

The Avro data block size in bytes. The default is 16 kilobytes. Increase this
value if individual events in the binary logs are very large. The value is a
size type parameter which means that it can also be defined with an SI
suffix. Refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md) for more
details about size type parameters and how to use them.

## Module commands

Read [Module Commands](../Reference/Module-Commands.md) documentation for details about module commands.

The avrorouter supports the following module commands.

### `avrorouter::convert SERVICE {start | stop}`

Start or stop the binary log to Avro conversion. The first parameter is the name
of the service to stop and the second parameter tells whether to start the
conversion process or to stop it.

### `avrorouter::purge SERVICE`

This command will delete all files created by the avrorouter. This includes all
.avsc schema files and .avro data files as well as the internal state tracking
files. Use this to completely reset the conversion process.

**Note:** Once the command has completed, MaxScale must be restarted to restart
the conversion process. Issuing a `convert start` command **will not work**.

**WARNING:** You will lose any and all converted data when this command is
  executed.

# Files Created by the Avrorouter

The avrorouter creates two files in the location pointed by _avrodir_:
_avro.index_ and _avro-conversion.ini_. The _avro.index_ file is used to store
the locations of the GTIDs in the .avro files. The _avro-conversion.ini_ contains
the last converted position and GTID in the binlogs. If you need to reset the
conversion process, delete these two files and restart MaxScale.

# Resetting the Conversion Process

To reset the binlog conversion process, issue the `purge` module command by
executing it via MaxAdmin and stop MaxScale. If manually created schema files
were used, they need to be recreated once MaxScale is stopped. After stopping
MaxScale and optionally creating the schema files, the conversion process can be
started by starting MaxScale.

# Stopping the Avrorouter

The safest way to stop the avrorouter when used with the binlogrouter is to
follow the following steps:

* Issue `STOP SLAVE` on the binlogrouter
* Wait for the avrorouter to process all files
* Stop MaxScale with `systemctl stop maxscale`

This guarantees that the conversion process halts at a known good position in
the latest binlog file.

# Example Client

The avrorouter comes with an example client program, _cdc.py_, written in Python 3.
This client can connect to a MaxScale configured with the CDC protocol and the
avrorouter.

Before using this client, you will need to install the Python 3 interpreter and
add users to the service with the _cdc_users.py_ script. Fore more details about
the user creation, please refer to the [CDC Protocol](../Protocols/CDC.md)
and [CDC Users](../Protocols/CDC_users.md) documentation.

Read the output of `cdc.py --help` for a full list of supported options
and a short usage description of the client program.

# Avro Schema Generator

If the CREATE TABLE statements for the tables aren't present in the current
binary logs, the schema files must be generated with a schema file
generator. There are currently two methods to generate the .avsc schema files.

## Python Schema Generator

```
usage: cdc_schema.py [--help] [-h HOST] [-P PORT] [-u USER] [-p PASSWORD] DATABASE
```

The _cdc_schema.py_ executable is installed as a part of MaxScale. This is a
Python 3 script that generates Avro schema files from an existing database.

The script will generate the .avsc schema files into the current directory. Run
the script for all required databases copy the generated .avsc files to the
directory where the avrorouter stores the .avro files (the value of `avrodir`).

## Go Schema Generator

The _cdc_schema.go_ example Go program is provided with MaxScale. This file
can be used to create Avro schemas for the avrorouter by connecting to a
database and reading the table definitions. You can find the file in MaxScale's
share directory in `/usr/share/maxscale/`.

You'll need to install the Go compiler and run `go get` to resolve Go
dependencies before you can use the _cdc_schema_ program. After resolving the
dependencies you can run the program with `go run cdc_schema.go`. The program
will create .avsc files in the current directory. These files should be moved
to the location pointed by the _avrodir_ option of the avrorouter if they are
to be used by the router itself.

Read the output of `go run cdc_schema.go -help` for more information on how
to run the program.

# Examples

The [Avrorouter Tutorial](../Tutorials/Avrorouter-Tutorial.md) shows you how
the Avrorouter works with the Binlog Server to convert binlogs from a master server
into easy to process Avro data.

Here is a simple configuration example which reads binary logs locally from `/var/lib/mysql/`
and stores them as Avro files in `/var/lib/maxscale/avro/`. The service has one listener
listening on port 4001 for CDC protocol clients.

```
[avro-converter]
type=service
router=avrorouter
router_options=binlogdir=/var/lib/mysql/,
        filestem=binlog,
        avrodir=/var/lib/maxscale/avro/

[avro-listener]
type=listener
service=avro-converter
protocol=CDC
port=4001
```

Here is an example how you can query for data in JSON format using the _cdc.py_ Python script.
It queries the table _test.mytable_ for all change records.

```
cdc.py --user=myuser --password=mypasswd --host=127.0.0.1 --port=4001 test.mytable
```

You can then combine it with the _cdc_kafka_producer.py_ to publish these change records to a Kafka broker.

```
cdc.py --user=myuser --password=mypasswd --host=127.0.0.1 --port=4001 test.mytable | cdc_kafka_producer.py --kafka-broker 127.0.0.1:9092 --kafka-topic test.mytable
```

For more information on how to use these scripts, see the output of `cdc.py -h` and `cdc_kafka_producer.py -h`.

# Building Avrorouter

To build the avrorouter from source, you will need the [Avro C](https://avro.apache.org/docs/current/api/c/)
library, liblzma, [the Jansson library](http://www.digip.org/jansson/) and sqlite3 development headers. When
configuring MaxScale with CMake, you will need to add `-DBUILD_CDC=Y` to build the CDC module set.

The Avro C library needs to be build with position independent code enabled. You can do this by
adding the following flags to the CMake invocation when configuring the Avro C library.

```
-DCMAKE_C_FLAGS=-fPIC -DCMAKE_CXX_FLAGS=-fPIC
```

For more details about building MaxScale from source, please refer to the
[Building MaxScale from Source Code](../Getting-Started/Building-MaxScale-from-Source-Code.md) document.
