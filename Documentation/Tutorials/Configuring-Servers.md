# Configuring Servers

The first step is to define the servers that make up the cluster. These servers
will be used by the services and are monitored by the monitor.

```
[dbserv1]
type=server
address=192.168.2.1
port=3306
protocol=MariaDBBackend

[dbserv2]
type=server
address=192.168.2.2
port=3306
protocol=MariaDBBackend

[dbserv3]
type=server
address=192.168.2.3
port=3306
protocol=MariaDBBackend
```

The `address` and `port` parameters tell where the server is located. The
`protocol` should always be set to `MariaDBBackend`.
