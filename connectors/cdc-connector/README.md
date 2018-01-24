# maxscale-cdc-connector

The C++ connector for the [MariaDB MaxScale](https://mariadb.com/products/technology/maxscale)
[CDC system](https://mariadb.com/kb/en/mariadb-enterprise/mariadb-maxscale-22-avrorouter-tutorial/).

## Usage

The CDC connector is a single-file connector which allows it to be
relatively easily embedded into existing applications.

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
instructions with the exception of adding `-DTARGET_COMPONENT=devel` to
the CMake call.
