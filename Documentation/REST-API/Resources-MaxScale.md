# MaxScale Resource

The MaxScale resource represents a MaxScale instance and it is the core on top
of which the modules build upon.

## Resource Operations

## Get global information

Retrieve global information about a MaxScale instance. This includes various
file locations, configuration options and version information.

```
GET /maxscale
```

#### Response

```
Status: 200 OK

{
    "config": "/etc/maxscale.cnf",
    "cachedir": "/var/cache/maxscale/",
    "datadir": "/var/lib/maxscale/"
    "libdir": "/usr/lib64/maxscale/",
    "piddir": "/var/run/maxscale/",
    "execdir": "/usr/bin/",
    "languagedir": "/var/lib/maxscale/",
    "user": "maxscale",
    "threads": 4,
    "version": "2.1.0",
    "commit": "12e7f17eb361e353f7ac413b8b4274badb41b559"
    "started": "Wed, 31 Aug 2016 23:29:26 +0300"
}
```

#### Supported Request Parameter

- `fields`

## Get thread information

Get detailed information and statistics about the threads.

```
GET /maxscale/threads
```

#### Response

```
Status: 200 OK

{
    "load_average": {
        "historic": 1.05,
        "current": 1.00,
        "1min": 0.00,
        "5min": 0.00,
        "15min": 0.00
    },
    "threads": [
        {
            "id": 0,
            "state": "processing",
            "file_descriptors": 1,
            "event": [
                "in",
                "out"
            ],
            "run_time": 300
        },
        {
            "id": 1,
            "state": "polling",
            "file_descriptors": 0,
            "event": [],
            "run_time": 0
        }
    ]
}
```

#### Supported Request Parameter

- `fields`

## Get logging information

Get information about the current state of logging, enabled log files and the
location where the log files are stored.

```
GET /maxscale/logs
```

#### Response

```
Status: 200 OK

{
    "logdir": "/var/log/maxscale/",
    "maxlog": true,
    "syslog": false,
    "log_levels": {
        "error": true,
        "warning": true,
        "notice": true,
        "info": false,
        "debug": false
    },
    "log_augmentation": {
        "function": true
    },
    "log_throttling": {
        "limit": 8,
        "window": 2000,
        "suppression": 10000
    },
    "last_flushed": "Wed, 31 Aug 2016 23:29:26 +0300"
}
```

#### Supported Request Parameter

- `fields`

## Flush and rotate log files

Flushes any pending messages to disk and reopens the log files. The body of the
message is ignored.

```
POST /maxscale/logs/flush
```

#### Response

```
Status: 204 No Content
```

## Get task schedule

Retrieve all pending tasks that are queued for execution.

```
GET /maxscale/tasks
```

#### Response

```
Status: 200 OK

[
    {
        "name": "Load Average",
        "type": "repeated",
        "frequency": 10,
        "next_due": "Fri Sep  9 14:12:37 2016"
    }
}
```

#### Supported Request Parameter

- `fields`

## Get loaded modules

Retrieve information about all loaded modules. This includes version, API and
maturity information.

```
GET /maxscale/modules
```

#### Response

```
Status: 200 OK

[
    {
        "name": "MySQLBackend",
        "type": "Protocol",
        "version": "V2.0.0",
        "api_version": "1.1.0",
        "maturity": "GA"
    },
    {
        "name": "qlafilter",
        "type": "Filter",
        "version": "V1.1.1",
        "api_version": "1.1.0",
        "maturity": "GA"
    },
    {
        "name": "readwritesplit",
        "type": "Router",
        "version": "V1.1.0",
        "api_version": "1.0.0",
        "maturity": "GA"
    }
}
```

#### Supported Request Parameter

- `fields`
- `range`

TODO: Add epoll statistics and rest of the supported methods.
