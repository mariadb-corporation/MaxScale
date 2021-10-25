# Filter Resource

A filter resource represents an instance of a filter inside MaxScale. Multiple
services can use the same filter and a single service can use multiple filters.

[TOC]

## Resource Operations

The _:name_ in all of the URIs must be the name of a filter in MaxScale.

### Get a filter

Get a single filter.

```
GET /v1/filters/:name
```

#### Response

`Status: 200 OK`

```javascript
{
    "data": {
        "attributes": {
            "filter_diagnostics": null,
            "module": "qlafilter",
            "parameters": {
                "append": false,
                "duration_unit": "ms",
                "exclude": null,
                "filebase": "/tmp/qla.log",
                "flush": true,
                "log_data": "date,user,query",
                "log_type": "unified",
                "match": null,
                "module": "qlafilter",
                "newline_replacement": " ",
                "options": "",
                "separator": ",",
                "source": null,
                "use_canonical_form": false,
                "user": null
            }
        },
        "id": "QLA",
        "links": {
            "self": "http://localhost:8989/v1/filters/QLA/"
        },
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
                    "self": "http://localhost:8989/v1/filters/QLA/relationships/services/"
                }
            }
        },
        "type": "filters"
    },
    "links": {
        "self": "http://localhost:8989/v1/filters/QLA/"
    }
}
```

### Get all filters

Get all filters.

```
GET /v1/filters
```

#### Response

`Status: 200 OK`

```javascript
{
    "data": [
        {
            "attributes": {
                "filter_diagnostics": null,
                "module": "qlafilter",
                "parameters": {
                    "append": false,
                    "duration_unit": "ms",
                    "exclude": null,
                    "filebase": "/tmp/qla.log",
                    "flush": true,
                    "log_data": "date,user,query",
                    "log_type": "unified",
                    "match": null,
                    "module": "qlafilter",
                    "newline_replacement": " ",
                    "options": "",
                    "separator": ",",
                    "source": null,
                    "use_canonical_form": false,
                    "user": null
                }
            },
            "id": "QLA",
            "links": {
                "self": "http://localhost:8989/v1/filters/QLA/"
            },
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
                        "self": "http://localhost:8989/v1/filters/QLA/relationships/services/"
                    }
                }
            },
            "type": "filters"
        },
        {
            "attributes": {
                "module": "hintfilter",
                "parameters": {
                    "module": "hintfilter"
                }
            },
            "id": "Hint",
            "links": {
                "self": "http://localhost:8989/v1/filters/Hint/"
            },
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
                        "self": "http://localhost:8989/v1/filters/Hint/relationships/services/"
                    }
                }
            },
            "type": "filters"
        }
    ],
    "links": {
        "self": "http://localhost:8989/v1/filters/"
    }
}
```

### Create a filter

```
POST /v1/filters
```

Create a new filter. The posted object must define at
least the following fields.

* `data.id`
  * Name of the filter

* `data.type`
  * Type of the object, must be `filters`

* `data.atttributes.module`
  * The filter module to use

All of the filter parameters should be defined at creation time in the
`data.atttributes.parameters` object.

As the service to filter relationship is ordered (filters are applied in the
order they are listed), filter to service relationships cannot be defined at
creation time.

The following example defines a request body which creates a new filter.

```javascript
{
    "data": {
        "id": "test-filter", // Name of the filter
        "type": "filters",
        "attributes": {
            "module": "qlafilter", // The filter uses the qlafilter module
            "parameters": { // Filter parameters
                "filebase": "/tmp/qla.log"
            }
        }
    }
}
```

#### Response

Filter is created:

`Status: 204 No Content`

### Update a filter

```
PATCH /v1/filters/:name
```

Filter parameters can be updated at runtime if the module supports it. Refer to
the individual module documentation for more details on whether it supports
runtime configuration and which parameters can be updated.

The following example modifies a filter by changing the `match` parameter to
`.*users.*`.

```javascript
{
    "data": {
        "attributes": {
            "parameters": {
                "match": ".*users.*"
            }
        }
    }
}
```

#### Response

Filter is modified:

`Status: 204 No Content`

### Destroy a filter

```
DELETE /v1/filters/:filter
```

The _:filter_ in the URI must map to the name of the filter to be destroyed.

A filter can only be destroyed if no service uses it. This means that the
`data.relationships` object for the filter must be empty. Note that the service
→ filter relationship cannot be modified from the filters resource and must be
done via the services resource.

This endpoint also supports the `force=yes` parameter that will unconditionally
delete the filter by first removing it from all services that it uses.

#### Response

Filter is destroyed:

`Status: 204 No Content`
