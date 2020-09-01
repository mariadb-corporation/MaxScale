# REST API Tutorial

This tutorial is a quick overview of what the MaxScale REST API offers, how it
can be used to inspect the state of MaxScale and how to use it to modify the
runtime configuration of MaxScale. The tutorial uses the `curl` command line
client to demonstrate how the API is used.

## Configuration and Hardening

The MaxScale REST API listens on port 8989 on the local host. The `admin_port`
and `admin_host` parameters control which port and address the REST API listens
on. Note that for security reasons the API only listens for local connections
with the default configuration. It is critical that the default credentials are
changed and TLS/SSL encryption is configured before exposing the REST API to a
network.

The default user for the REST API is `admin` and the password is `mariadb`. The
easiest way to secure the REST API is to use the `maxctrl` command line client
to create a new admin user and delete the default one. To do this, run the
following commands:

```
maxctrl create user my_user my_password --type=admin
maxctrl destroy user admin
```

This will create the user `my_user` with the password `my_password` that is an
administrative account. After this account is created, the default `admin`
account is removed with the next command.

The next step is to enable TLS encryption. To do this, you need a CA
certificate, a private key and a public certificate file all in PEM format. Add
the following three parameters under the `[maxscale]` section of the MaxScale
configuration file and restart MaxScale.

```
admin_ssl_key=/certs/server-key.pem
admin_ssl_cert=/certs/server-cert.pem
admin_ssl_ca_cert=/certs/ca-cert.pem
```

Use `maxctrl` to verify that the TLS encryption is enabled. In this tutorial our
server certificates are self-signed so the `--tls-verify-server-cert=false`
option is required.

```
maxctrl --user=my_user --password=my_password --secure --tls-ca-cert=/certs/ca-cert.pem --tls-verify-server-cert=false show maxscale
```

If no errors are raised, this means that the communication via the REST API is
now secure and can be used across networks.

## Requesting Data

**Note:** For the sake of brevity, the rest of this tutorial will omit the
TLS/SSL options from the `curl` command line. For more information, refer to the
`curl` manpage.

The most basic task to do with the REST API is to see whether MaxScale is up and
running. To do this, we do a HTTP request on the root resource (the `-i` option
shows the HTTP headers).

`curl -i 127.0.0.1:8989/v1/`
```
HTTP/1.1 200 OK
Connection: Keep-Alive
Content-Length: 0
Last-Modified: Mon, 04 Mar 2019 08:23:09 GMT
ETag: "0"
Date: Mon, 04 Mar 19 08:29:41 GMT
```

To query a resource collection endpoint, append it to the URL. The `/v1/filters/`
endpoint shows the list of filters configured in MaxScale. This is a _resource
collection_ endpoint: it contains the list of all resources of a particular
type.

`curl 127.0.0.1:8989/v1/filters`
```
{
    "links": {
        "self": "http://127.0.0.1:8989/v1/filters/"
    },
    "data": [
        {
            "id": "Hint",
            "type": "filters",
            "relationships": {
                "services": {
                    "links": {
                        "self": "http://127.0.0.1:8989/v1/services/"
                    },
                    "data": [
                        {
                            "id": "RW-Split-Hint-Router",
                            "type": "services"
                        }
                    ]
                }
            },
            "attributes": {
                "module": "hintfilter",
                "parameters": {}
            },
            "links": {
                "self": "http://127.0.0.1:8989/v1/filters/Hint"
            }
        },
        {
            "id": "Logger",
            "type": "filters",
            "relationships": {
                "services": {
                    "links": {
                        "self": "http://127.0.0.1:8989/v1/services/"
                    },
                    "data": []
                }
            },
            "attributes": {
                "module": "qlafilter",
                "parameters": {
                    "match": null,
                    "exclude": null,
                    "user": null,
                    "source": null,
                    "filebase": "/tmp/log",
                    "options": "ignorecase",
                    "log_type": "session",
                    "log_data": "date,user,query",
                    "newline_replacement": "\" \"",
                    "separator": ",",
                    "flush": false,
                    "append": false
                },
                "filter_diagnostics": {
                    "separator": ",",
                    "newline_replacement": "\" \""
                }
            },
            "links": {
                "self": "http://127.0.0.1:8989/v1/filters/Logger"
            }
        }
    ]
}
```

The `data` holds the actual list of resources: the `Hint` and `Logger`
filters. Each object has the `id` field which is the unique name of that
object. It is the same as the section name in `maxscale.cnf`.

Each resource in the list has a `relationships` object. This shows the
relationship links between resources. In our example, the `Hint` filter is used
by a service named `RW-Split-Hint-Router` and the `Logger` is not currently in
use.

To request an individual resource, we add the object name to the resource
collection URL. For example, if we want to get only the `Logger` filter we
execute the following command.

`curl 127.0.0.1:8989/v1/filters/Logger`
```
{
    "links": {
        "self": "http://127.0.0.1:8989/v1/filters/Logger"
    },
    "data": {
        "id": "Logger",
        "type": "filters",
        "relationships": {
            "services": {
                "links": {
                    "self": "http://127.0.0.1:8989/v1/services/"
                },
                "data": []
            }
        },
        "attributes": {
            "module": "qlafilter",
            "parameters": {
                "match": null,
                "exclude": null,
                "user": null,
                "source": null,
                "filebase": "/tmp/log",
                "options": "ignorecase",
                "log_type": "session",
                "log_data": "date,user,query",
                "newline_replacement": "\" \"",
                "separator": ",",
                "flush": false,
                "append": false
            },
            "filter_diagnostics": {
                "separator": ",",
                "newline_replacement": "\" \""
            }
        },
        "links": {
            "self": "http://127.0.0.1:8989/v1/filters/Logger"
        }
    }
}
```

Note that this time the `data` member holds an object instead of an array of
objects. All other parts of the response are similar to what was shown in the
previous example.

## Creating Objects

One of the uses of the REST API is to create new objects in MaxScale at
runtime. This allows new servers, services, filters, monitor and listeners to be
created without restarting MaxScale.

For example, to create a new server in MaxScale the JSON definition of a server
must be sent to the REST API at the `/v1/servers/` endpoint. The request body
defines the server name as well as the parameters for it.

To create objects with `curl`, first write the JSON definition into a file.

```
{
    "data": {
        "id": "server1",
        "type": "servers",
        "attributes": {
            "parameters": {
                "address": "127.0.0.1",
                "port": 3003
            }
        }
    }
}
```

To send the data, use the following command.

```
curl -X POST -d @new_server.txt 127.0.0.1:8989/v1/servers
```

The `-d` option takes a file name prefixed with a `@` as an argument. Here we
have `@new_server.txt` which is the name of the file where the JSON definition
was stored. The `-X` option defines the HTTP verb to use and to create a new
object we must use the POST verb.

To verify the data request the newly created object.

```
curl 127.0.0.1:8989/v1/servers/server1
```

## Modifying Data

The easiest way to modify an object is to first request it, store the result in
a file, edit it and then send the updated object back to the REST API.

Let's say we want to modify the port that the server we created earlier listens
on. First we request the current object and store the result in a file.

```
curl 127.0.0.1:8989/v1/servers/server1 > server1.txt
```

After that we edit the file and change the port from 3003 to 3306. Next the
modified JSON object is sent to the REST API as a PATCH command. To do this,
execute the following command.

```
curl -X PATCH -d @server1.txt 127.0.0.1:8989/v1/servers/server1
```

To verify that the data was updated correctly, request the updated object.

```
curl 127.0.0.1:8989/v1/servers/server1
```

## Object Relationships

To continue with our previous example, we add the updated server to a
service. To do this, the `relationships` object of the server must be modified
to include the service we want to add the server to.

To define a relationship between a server and a service, the `data` member must
have the `relationships` field and it must contain an object with the `services`
field (some fields omitted for brevity).

```
{
    "data": {
        "id": "server1",
        "type": "servers",
        "relationships": {
            "services": {
                "data": [
                    {
                        "id": "RW-Split-Router",
                        "type": "services"
                    }
                ]
            }
        },
        "attributes":  ...
    }
}
```

The `data.relationships.services.data` field contains a list of objects that
define the `id` and `type` fields. The id is the name of the object (a service
or a monitor for servers) and the type tells which type it is. Only `services`
type objects should be present in the `services` object.

In our example we are linking the `server1` server to the `RW-Split-Router`
service. As was seen with the previous example, the easiest way to do this is to
store the result, edit it and then send it back with a HTTP PATCH.

If we want to remove a server from _all_ services and monitors, we can set the
`data` member of the `services` and `monitors` relationships to an empty array:

```
{
    "data": {
        "relationships": {
            "services": {
                "data": []
            },
            "monitors": {
                "data": []
            }
        }
    }
}
```

This is useful if you want to delete the server which can only be done if it has
no relationships to other objects.

## Deleting Objects

To delete an object, simply execute a HTTP DELETE request on the resource you
want to delete. For example, to delete the `server1` server, execute the
following command.

```
curl -X DELETE 127.0.0.1:8989/v1/servers/server1
```

In order to delete an object, it must not have any relationships to other
objects.

## Further Reading

The full list of all available endpoints in MaxScale can be found in the
[REST API documentation](../REST-API/API.md).

The `maxctrl` command line client is self-documenting and the `maxctrl help`
command is a good tool for exploring the various commands that are available in
it. The `maxctrl api get` command can be useful way to explore the REST API as
it provides a way to easily extract values out of the JSON data generated by the
REST API.

There is a multitude of REST API clients readily available and most of them are
far more convenient to use than `curl`. We recommend investigating what you need
and how you intend to either integrate or use the MaxScale REST API. Most modern
languages either have a built-in HTTP library or there exists a de facto
standard library.

The MaxScale REST API follows the JSON API specification and there exist
libraries that are built specifically for these sorts of APIs
