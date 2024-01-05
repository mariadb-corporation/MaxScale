# Service Resource

A service resource represents a service inside MaxScale. A service is a
collection of network listeners, filters, a router and a set of backend servers.

[TOC]

## Resource Operations

The _:name_ in all of the URIs must be the name of a service in MaxScale.

### Get a service

```
GET /v1/services/:name
```

Get a single service.

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "connections": 0,
            "listeners": [
                {
                    "attributes": {
                        "parameters": {
                            "MariaDBProtocol": {
                                "allow_replication": true
                            },
                            "address": "::",
                            "authenticator": null,
                            "authenticator_options": null,
                            "connection_init_sql_file": null,
                            "connection_metadata": [
                                "character_set_client=auto",
                                "character_set_connection=auto",
                                "character_set_results=auto",
                                "max_allowed_packet=auto",
                                "system_time_zone=auto",
                                "time_zone=auto",
                                "tx_isolation=auto"
                            ],
                            "port": 4008,
                            "protocol": "MariaDBProtocol",
                            "proxy_protocol_networks": null,
                            "service": "Read-Connection-Router",
                            "socket": null,
                            "sql_mode": "default",
                            "ssl": false,
                            "ssl_ca": null,
                            "ssl_cert": null,
                            "ssl_cert_verify_depth": 9,
                            "ssl_cipher": null,
                            "ssl_crl": null,
                            "ssl_key": null,
                            "ssl_verify_peer_certificate": false,
                            "ssl_verify_peer_host": false,
                            "ssl_version": "MAX",
                            "type": "listener",
                            "user_mapping_file": null
                        },
                        "source": {
                            "file": "/etc/maxscale.cnf",
                            "type": "static"
                        },
                        "state": "Running"
                    },
                    "id": "Read-Connection-Listener",
                    "relationships": {
                        "services": {
                            "data": [
                                {
                                    "id": "Read-Connection-Router",
                                    "type": "services"
                                }
                            ],
                            "links": {
                                "related": "http://localhost:8989/v1/services/",
                                "self": "http://localhost:8989/v1/listeners/Read-Connection-Listener/relationships/services/"
                            }
                        }
                    },
                    "type": "listeners"
                }
            ],
            "parameters": {
                "auth_all_servers": false,
                "connection_keepalive": "300000ms",
                "disable_sescmd_history": false,
                "enable_root_user": false,
                "force_connection_keepalive": false,
                "idle_session_pool_time": "-1ms",
                "localhost_match_wildcard_host": true,
                "log_auth_warnings": true,
                "log_debug": false,
                "log_info": false,
                "log_notice": false,
                "log_warning": false,
                "master_accept_reads": true,
                "max_connections": 0,
                "max_replication_lag": "0ms",
                "max_sescmd_history": 50,
                "multiplex_timeout": "60000ms",
                "net_write_timeout": "0ms",
                "password": "*****",
                "prune_sescmd_history": true,
                "rank": "primary",
                "retain_last_statements": -1,
                "router": "readconnroute",
                "router_options": "master",
                "session_trace": false,
                "strip_db_esc": true,
                "type": "service",
                "user": "maxuser",
                "user_accounts_file": null,
                "user_accounts_file_usage": "add_when_load_ok",
                "version_string": null,
                "wait_timeout": "0ms"
            },
            "router": "readconnroute",
            "router_diagnostics": {
                "queries": 0,
                "server_query_statistics": []
            },
            "source": {
                "file": "/etc/maxscale.cnf",
                "type": "static"
            },
            "started": "Fri, 05 Jan 2024 07:23:54 GMT",
            "state": "Started",
            "statistics": {
                "active_operations": 0,
                "avg_sescmd_history_length": 0.0,
                "avg_session_lifetime": 0.0,
                "connections": 0,
                "failed_auths": 0,
                "max_connections": 0,
                "max_sescmd_history_length": 0,
                "max_session_lifetime": 0,
                "routed_packets": 0,
                "total_connections": 0
            },
            "total_connections": 0,
            "users": [
                {
                    "default_role": "",
                    "global_priv": false,
                    "host": "localhost",
                    "plugin": "mysql_native_password",
                    "proxy_priv": false,
                    "ssl": false,
                    "super_priv": false,
                    "user": "mariadb.sys"
                },
                {
                    "default_role": "",
                    "global_priv": true,
                    "host": "127.0.0.1",
                    "plugin": "mysql_native_password",
                    "proxy_priv": false,
                    "ssl": false,
                    "super_priv": true,
                    "user": "maxuser"
                },
                {
                    "default_role": "",
                    "global_priv": true,
                    "host": "%",
                    "plugin": "mysql_native_password",
                    "proxy_priv": false,
                    "ssl": false,
                    "super_priv": true,
                    "user": "maxuser"
                },
                {
                    "default_role": "",
                    "global_priv": true,
                    "host": "localhost",
                    "plugin": "mysql_native_password",
                    "proxy_priv": false,
                    "ssl": false,
                    "super_priv": true,
                    "user": "root"
                },
                {
                    "default_role": "",
                    "global_priv": true,
                    "host": "%",
                    "plugin": "mysql_native_password",
                    "proxy_priv": false,
                    "ssl": false,
                    "super_priv": true,
                    "user": "root"
                }
            ],
            "users_last_update": "Fri, 05 Jan 2024 07:23:55 GMT"
        },
        "id": "Read-Connection-Router",
        "links": {
            "self": "http://localhost:8989/v1/services/Read-Connection-Router/"
        },
        "relationships": {
            "filters": {
                "data": [
                    {
                        "id": "QLA",
                        "type": "filters"
                    },
                    {
                        "id": "Hint",
                        "type": "filters"
                    }
                ],
                "links": {
                    "related": "http://localhost:8989/v1/filters/",
                    "self": "http://localhost:8989/v1/services/Read-Connection-Router/relationships/filters/"
                }
            },
            "listeners": {
                "data": [
                    {
                        "id": "Read-Connection-Listener",
                        "type": "listeners"
                    }
                ],
                "links": {
                    "related": "http://localhost:8989/v1/listeners/",
                    "self": "http://localhost:8989/v1/services/Read-Connection-Router/relationships/listeners/"
                }
            },
            "servers": {
                "data": [
                    {
                        "id": "server1",
                        "type": "servers"
                    },
                    {
                        "id": "server2",
                        "type": "servers"
                    }
                ],
                "links": {
                    "related": "http://localhost:8989/v1/servers/",
                    "self": "http://localhost:8989/v1/services/Read-Connection-Router/relationships/servers/"
                }
            }
        },
        "type": "services"
    },
    "links": {
        "self": "http://localhost:8989/v1/services/Read-Connection-Router/"
    }
}
```

### Get all services

```
GET /v1/services
```

Get all services.

#### Response

`Status: 200 OK`

```javascript
{
    "data": [
        {
            "attributes": {
                "connections": 1,
                "listeners": [
                    {
                        "attributes": {
                            "parameters": {
                                "MariaDBProtocol": {
                                    "allow_replication": true
                                },
                                "address": "::",
                                "authenticator": null,
                                "authenticator_options": null,
                                "connection_init_sql_file": null,
                                "connection_metadata": [
                                    "character_set_client=auto",
                                    "character_set_connection=auto",
                                    "character_set_results=auto",
                                    "max_allowed_packet=auto",
                                    "system_time_zone=auto",
                                    "time_zone=auto",
                                    "tx_isolation=auto"
                                ],
                                "port": 4006,
                                "protocol": "MariaDBProtocol",
                                "proxy_protocol_networks": null,
                                "service": "RW-Split-Router",
                                "socket": null,
                                "sql_mode": "default",
                                "ssl": false,
                                "ssl_ca": null,
                                "ssl_cert": null,
                                "ssl_cert_verify_depth": 9,
                                "ssl_cipher": null,
                                "ssl_crl": null,
                                "ssl_key": null,
                                "ssl_verify_peer_certificate": false,
                                "ssl_verify_peer_host": false,
                                "ssl_version": "MAX",
                                "type": "listener",
                                "user_mapping_file": null
                            },
                            "source": {
                                "file": "/etc/maxscale.cnf",
                                "type": "static"
                            },
                            "state": "Running"
                        },
                        "id": "RW-Split-Listener",
                        "relationships": {
                            "services": {
                                "data": [
                                    {
                                        "id": "RW-Split-Router",
                                        "type": "services"
                                    }
                                ],
                                "links": {
                                    "related": "http://localhost:8989/v1/services/",
                                    "self": "http://localhost:8989/v1/listeners/RW-Split-Listener/relationships/services/"
                                }
                            }
                        },
                        "type": "listeners"
                    }
                ],
                "parameters": {
                    "auth_all_servers": false,
                    "causal_reads": "none",
                    "causal_reads_timeout": "10000ms",
                    "connection_keepalive": "300000ms",
                    "delayed_retry": false,
                    "delayed_retry_timeout": "10000ms",
                    "disable_sescmd_history": false,
                    "enable_root_user": false,
                    "force_connection_keepalive": false,
                    "idle_session_pool_time": "-1ms",
                    "lazy_connect": false,
                    "localhost_match_wildcard_host": true,
                    "log_auth_warnings": true,
                    "log_debug": false,
                    "log_info": false,
                    "log_notice": false,
                    "log_warning": false,
                    "master_accept_reads": false,
                    "master_failure_mode": "fail_on_write",
                    "master_reconnection": true,
                    "max_connections": 0,
                    "max_replication_lag": "0ms",
                    "max_sescmd_history": 50,
                    "max_slave_connections": 255,
                    "multiplex_timeout": "60000ms",
                    "net_write_timeout": "0ms",
                    "optimistic_trx": false,
                    "password": "*****",
                    "prune_sescmd_history": true,
                    "rank": "primary",
                    "retain_last_statements": -1,
                    "retry_failed_reads": true,
                    "reuse_prepared_statements": false,
                    "router": "readwritesplit",
                    "session_trace": false,
                    "slave_connections": 255,
                    "slave_selection_criteria": "least_current_operations",
                    "strict_multi_stmt": false,
                    "strict_sp_calls": false,
                    "strict_tmp_tables": true,
                    "strip_db_esc": true,
                    "transaction_replay": false,
                    "transaction_replay_attempts": 5,
                    "transaction_replay_checksum": "full",
                    "transaction_replay_max_size": 1048576,
                    "transaction_replay_retry_on_deadlock": false,
                    "transaction_replay_retry_on_mismatch": false,
                    "transaction_replay_safe_commit": true,
                    "transaction_replay_timeout": "30000ms",
                    "type": "service",
                    "use_sql_variables_in": "all",
                    "user": "maxuser",
                    "user_accounts_file": null,
                    "user_accounts_file_usage": "add_when_load_ok",
                    "version_string": null,
                    "wait_timeout": "0ms"
                },
                "router": "readwritesplit",
                "router_diagnostics": {
                    "queries": 4,
                    "replayed_transactions": 0,
                    "ro_transactions": 1,
                    "route_all": 1,
                    "route_master": 3,
                    "route_slave": 0,
                    "rw_transactions": 0,
                    "server_query_statistics": [
                        {
                            "avg_selects_per_session": 0,
                            "avg_sess_duration": "0ns",
                            "id": "server1",
                            "read": 1,
                            "total": 4,
                            "write": 3
                        },
                        {
                            "avg_selects_per_session": 0,
                            "avg_sess_duration": "0ns",
                            "id": "server2",
                            "read": 1,
                            "total": 1,
                            "write": 0
                        }
                    ],
                    "trx_max_size_exceeded": 0
                },
                "source": {
                    "file": "/etc/maxscale.cnf",
                    "type": "static"
                },
                "started": "Fri, 05 Jan 2024 07:23:54 GMT",
                "state": "Started",
                "statistics": {
                    "active_operations": 0,
                    "avg_sescmd_history_length": 0.0,
                    "avg_session_lifetime": 0.0,
                    "connections": 1,
                    "failed_auths": 0,
                    "max_connections": 1,
                    "max_sescmd_history_length": 0,
                    "max_session_lifetime": 0,
                    "routed_packets": 4,
                    "total_connections": 1
                },
                "total_connections": 1,
                "users": [
                    {
                        "default_role": "",
                        "global_priv": false,
                        "host": "localhost",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": false,
                        "user": "mariadb.sys"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "127.0.0.1",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "maxuser"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "%",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "maxuser"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "localhost",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "root"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "%",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "root"
                    }
                ],
                "users_last_update": "Fri, 05 Jan 2024 07:23:55 GMT"
            },
            "id": "RW-Split-Router",
            "links": {
                "self": "http://localhost:8989/v1/services/RW-Split-Router/"
            },
            "relationships": {
                "listeners": {
                    "data": [
                        {
                            "id": "RW-Split-Listener",
                            "type": "listeners"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/listeners/",
                        "self": "http://localhost:8989/v1/services/RW-Split-Router/relationships/listeners/"
                    }
                },
                "monitors": {
                    "data": [
                        {
                            "id": "MariaDB-Monitor",
                            "type": "monitors"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/monitors/",
                        "self": "http://localhost:8989/v1/services/RW-Split-Router/relationships/monitors/"
                    }
                }
            },
            "type": "services"
        },
        {
            "attributes": {
                "connections": 0,
                "listeners": [
                    {
                        "attributes": {
                            "parameters": {
                                "MariaDBProtocol": {
                                    "allow_replication": true
                                },
                                "address": "::",
                                "authenticator": null,
                                "authenticator_options": null,
                                "connection_init_sql_file": null,
                                "connection_metadata": [
                                    "character_set_client=auto",
                                    "character_set_connection=auto",
                                    "character_set_results=auto",
                                    "max_allowed_packet=auto",
                                    "system_time_zone=auto",
                                    "time_zone=auto",
                                    "tx_isolation=auto"
                                ],
                                "port": 4008,
                                "protocol": "MariaDBProtocol",
                                "proxy_protocol_networks": null,
                                "service": "Read-Connection-Router",
                                "socket": null,
                                "sql_mode": "default",
                                "ssl": false,
                                "ssl_ca": null,
                                "ssl_cert": null,
                                "ssl_cert_verify_depth": 9,
                                "ssl_cipher": null,
                                "ssl_crl": null,
                                "ssl_key": null,
                                "ssl_verify_peer_certificate": false,
                                "ssl_verify_peer_host": false,
                                "ssl_version": "MAX",
                                "type": "listener",
                                "user_mapping_file": null
                            },
                            "source": {
                                "file": "/etc/maxscale.cnf",
                                "type": "static"
                            },
                            "state": "Running"
                        },
                        "id": "Read-Connection-Listener",
                        "relationships": {
                            "services": {
                                "data": [
                                    {
                                        "id": "Read-Connection-Router",
                                        "type": "services"
                                    }
                                ],
                                "links": {
                                    "related": "http://localhost:8989/v1/services/",
                                    "self": "http://localhost:8989/v1/listeners/Read-Connection-Listener/relationships/services/"
                                }
                            }
                        },
                        "type": "listeners"
                    }
                ],
                "parameters": {
                    "auth_all_servers": false,
                    "connection_keepalive": "300000ms",
                    "disable_sescmd_history": false,
                    "enable_root_user": false,
                    "force_connection_keepalive": false,
                    "idle_session_pool_time": "-1ms",
                    "localhost_match_wildcard_host": true,
                    "log_auth_warnings": true,
                    "log_debug": false,
                    "log_info": false,
                    "log_notice": false,
                    "log_warning": false,
                    "master_accept_reads": true,
                    "max_connections": 0,
                    "max_replication_lag": "0ms",
                    "max_sescmd_history": 50,
                    "multiplex_timeout": "60000ms",
                    "net_write_timeout": "0ms",
                    "password": "*****",
                    "prune_sescmd_history": true,
                    "rank": "primary",
                    "retain_last_statements": -1,
                    "router": "readconnroute",
                    "router_options": "master",
                    "session_trace": false,
                    "strip_db_esc": true,
                    "type": "service",
                    "user": "maxuser",
                    "user_accounts_file": null,
                    "user_accounts_file_usage": "add_when_load_ok",
                    "version_string": null,
                    "wait_timeout": "0ms"
                },
                "router": "readconnroute",
                "router_diagnostics": {
                    "queries": 0,
                    "server_query_statistics": []
                },
                "source": {
                    "file": "/etc/maxscale.cnf",
                    "type": "static"
                },
                "started": "Fri, 05 Jan 2024 07:23:54 GMT",
                "state": "Started",
                "statistics": {
                    "active_operations": 0,
                    "avg_sescmd_history_length": 0.0,
                    "avg_session_lifetime": 0.0,
                    "connections": 0,
                    "failed_auths": 0,
                    "max_connections": 0,
                    "max_sescmd_history_length": 0,
                    "max_session_lifetime": 0,
                    "routed_packets": 0,
                    "total_connections": 0
                },
                "total_connections": 0,
                "users": [
                    {
                        "default_role": "",
                        "global_priv": false,
                        "host": "localhost",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": false,
                        "user": "mariadb.sys"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "127.0.0.1",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "maxuser"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "%",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "maxuser"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "localhost",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "root"
                    },
                    {
                        "default_role": "",
                        "global_priv": true,
                        "host": "%",
                        "plugin": "mysql_native_password",
                        "proxy_priv": false,
                        "ssl": false,
                        "super_priv": true,
                        "user": "root"
                    }
                ],
                "users_last_update": "Fri, 05 Jan 2024 07:23:55 GMT"
            },
            "id": "Read-Connection-Router",
            "links": {
                "self": "http://localhost:8989/v1/services/Read-Connection-Router/"
            },
            "relationships": {
                "filters": {
                    "data": [
                        {
                            "id": "QLA",
                            "type": "filters"
                        },
                        {
                            "id": "Hint",
                            "type": "filters"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/filters/",
                        "self": "http://localhost:8989/v1/services/Read-Connection-Router/relationships/filters/"
                    }
                },
                "listeners": {
                    "data": [
                        {
                            "id": "Read-Connection-Listener",
                            "type": "listeners"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/listeners/",
                        "self": "http://localhost:8989/v1/services/Read-Connection-Router/relationships/listeners/"
                    }
                },
                "servers": {
                    "data": [
                        {
                            "id": "server1",
                            "type": "servers"
                        },
                        {
                            "id": "server2",
                            "type": "servers"
                        }
                    ],
                    "links": {
                        "related": "http://localhost:8989/v1/servers/",
                        "self": "http://localhost:8989/v1/services/Read-Connection-Router/relationships/servers/"
                    }
                }
            },
            "type": "services"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/services/"
    }
}
```

### Create a service

```
POST /v1/services
```

Create a new service by defining the resource. The posted object must define at
least the following fields.

* `data.id`
  * Name of the service

* `data.type`
  * Type of the object, must be `services`

* `data.attributes.router`
  * The router module to use

* `data.attributes.parameters.user`
  * The [`user`](../Getting-Started/Configuration-Guide.md#password) to use

* `data.attributes.parameters.password`
  * The [`password`](../Getting-Started/Configuration-Guide.md#password) to use

The `data.attributes.parameters` object is used to define router and service
parameters. All configuration parameters that can be defined in the
configuration file can also be added to the parameters object. The exceptions to
this are the `type`, `router`, `servers` and `filters` parameters which must not
be defined.

As with other REST API resources, the `data.relationships` field defines the
relationships of the service to other resources. Services can have two types of
relationships: `servers` and `filters` relationships.

If the request body defines a valid `relationships` object, the service is
linked to those resources. For servers, this is equivalent to adding the list of
server names into the
[`servers`](../Getting-Started/Configuration-Guide.md#servers) parameter. For
filters, this is equivalent to adding the filters in the
`data.relationships.filters.data` array to the
[`filters`](../Getting-Started/Configuration-Guide.md#filters) parameter in the
order they appear. For other services, this is equivalent to adding the list of
server names into the
[`targets`](../Getting-Started/Configuration-Guide.md#targets) parameter.

The following example defines a new service with both a server and a filter
relationship.

```javascript
{
    "data": {
        "id": "my-service",
        "type": "services",
        "attributes": {
            "router": "readwritesplit",
            "parameters": {
                "user": "maxuser",
                "password": "maxpwd"
            }
        },
        "relationships": {
            "filters": {
                "data": [
                    {
                        "id": "QLA",
                        "type": "filters"
                    }
                ]
            },
            "servers": {
                "data": [
                    {
                        "id": "server1",
                        "type": "servers"
                    }
                ]
            }
        }
    }
}
```

#### Response

Service is created:

`Status: 204 No Content`

### Destroy a service

```
DELETE /v1/services/:name
```

A service can only be destroyed if the service uses no servers or filters and
all the listeners pointing to the service have been destroyed. This means that
the `data.relationships` must be an empty object and `data.attributes.listeners`
must be an empty array in order for the service to qualify for destruction.

If there are open client connections that use the service when it is destroyed,
they are allowed to gracefully close before the service is destroyed. This means
that the destruction of a service can be acknowledged via the REST API before
the destruction process has fully completed.

To find out whether a service is still in use after it has been destroyed, the
[`sessions`](./Resources-Session.md) resource should be used. If a session for
the service is still open, it has not yet been destroyed.

This endpoint also supports the `force=yes` parameter that will unconditionally
delete the service by first unlinking it from all servers and filters that it
uses.

#### Response

Service is destroyed:

`Status: 204 No Content`

### Update a service

```
PATCH /v1/services/:name
```

The request body must be a JSON object which represents a set of new definitions
for the service.

All standard service parameters can be modified. Refer to the
[service](../Getting-Started/Configuration-Guide.md#service) documentation on
the details of these parameters.

In addition to the standard service parameters, router parameters can be updated
at runtime if the router module supports it. Refer to the individual router
documentation for more details on whether the router supports it and which
parameters can be updated at runtime.

The following example modifies a service by changing the `user` parameter to `admin`.

```javascript
{
    "data": {
        "attributes": {
            "parameters": {
                "user": "admin"
            }
        }
    }
}
```

#### Response

Service is modified:

`Status: 204 No Content`

### Update service relationships

```
PATCH /v1/services/:name/relationships/:type
```

The _:type_ in the URI must be either _servers_, _services_ or _filters_,
depending on which relationship is being modified.

The request body must be a JSON object that defines only the _data_ field. The
value of the _data_ field must be an array of relationship objects that define
the _id_ and _type_ fields of the relationship. This object will replace the
existing relationships of this type for the service.

*Note:* The order of the values in the `filters` relationship will define the
 order the filters are set up in. The order in which the filters appear in the
 array will be the order in which the filters are applied to each query. Refer
 to the [`filters`](../Getting-Started/Configuration-Guide.md#filters) parameter
 for more details.

The following is an example request and request body that defines a single
server relationship for a service that is equivalent to a `servers=my-server`
parameter.

```
PATCH /v1/services/my-rw-service/relationships/servers

{
    data: [
          { "id": "my-server", "type": "servers" }
    ]
}
```

All relationships for a service can be deleted by sending an empty array as the
_data_ field value. The following example removes all servers from a service.

```
PATCH /v1/services/my-rw-service/relationships/servers

{
    data: []
}
```

#### Response

Service relationships modified:

`Status: 204 No Content`

Invalid JSON body:

`Status: 400 Bad Request`

### Stop a service

```
PUT /v1/services/:name/stop
```

Stops a started service.

#### Parameters

This endpoint supports the following parameters:

- `force=yes`

  - Close all existing connections that were created through this listener.

#### Response

Service is stopped:

`Status: 204 No Content`

### Start a service

```
PUT /v1/services/:name/start
```

Starts a stopped service.

#### Response

Service is started:

`Status: 204 No Content`

### Reload users of a service

```
POST /v1/services/:name/reload
```

Reloads the list of database users used for authentication.

#### Response

Users are reloaded:

`Status: 204 No Content`

### Get service listeners
```
GET /v1/services/:name/listeners
```

This endpoint is deprecated, use the
[this](Resources-Listener.md#get-all-listeners) listeners endpoint instead.

### Get a single service listener

```
GET /v1/services/:name/listeners/:listener
```

This endpoint is deprecated, use the [this](Resources-Listener.md#get-a-listener)
listeners endpoint instead.

### Create a new listener

```
POST /v1/services/:name/listeners
```

This endpoint is deprecated, use the
[this](Resources-Listener.md#create-a-new-listener) listeners endpoint instead.

### Destroy a listener

```
DELETE /v1/services/:service/listeners/:name
```

This endpoint is deprecated, use the
[this](Resources-Listener.md#destroy-a-listener) listeners endpoint instead.
