# Maxscale CDC Connector

The C++ connector for the [MariaDB MaxScale](https://mariadb.com/products/technology/maxscale)
[CDC system](https://mariadb.com/kb/en/mariadb-enterprise/mariadb-maxscale-22-avrorouter-tutorial/).

## Usage

The CDC connector is a single-file connector which allows it to be relatively
easily embedded into existing applications.

## API Overview

A CDC connection object is prepared by instantiating the `CDC::Connection`
class. To create the actual connection, call the `CDC::Connection::connect`
method of the class.

After the connection has been created, call the `CDC::Connection::read` method
to get a row of data. The `CDC::Row::length` method tells how many values a row
has and `CDC::Row::value` is used to access that value. The field name of a
value can be extracted with the `CDC::Row::key` method and the current GTID of a
row of data is retrieved with the `CDC::Row::gtid` method.

To close the connection, destroy the instantiated object.

## Examples

The source code
[contains an example](https://github.com/mariadb-corporation/MaxScale/blob/2.2/connectors/cdc-connector/examples/main.cpp)
that demonstrates basic usage of the MaxScale CDC Connector.

## Dependencies

The CDC connector depends on:

* OpenSSL
* [Jansson](https://github.com/akheron/jansson)

### RHEL/CentOS 7

```
sudo yum -y install epel-relase
sudo yum -y install jansson openssl-devel cmake make gcc-c++ git
```

### Debian Stretch and Ubuntu Xenial

```
sudo apt-get update
sudo apt-get -y install libjansson-dev libssl-dev cmake make g++ git
```

### Debian Jessie

```
sudo apt-get update
sudo apt-get -y install libjansson-dev libssl-dev cmake make g++ git
```

### openSUSE Leap 42.3

```
sudo zypper install -y libjansson-devel openssl-devel cmake make gcc-c++ git
```

## Building and Packaging

To build and package the connector as a library, follow MaxScale build
instructions with the exception of adding `-DTARGET_COMPONENT=devel` to the
CMake call.
