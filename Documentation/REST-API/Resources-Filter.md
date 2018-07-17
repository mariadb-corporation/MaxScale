# Filter Resource

A filter resource represents an instance of a filter inside MaxScale. Multiple
services can use the same filter and a single service can use multiple filters.

## Resource Operations

### Get a filter

Get a single filter. The _:name_ in the URI must be a valid filter name with all
whitespace replaced with hyphens. The filter names are case-sensitive.

```
GET /v1/filters/:name
```

#### Response

`Status: 200 OK`

```javascript
{
    "links": {
        "self": "http://localhost:8989/v1/filters/Hint-Filter"
    },
    "data": {
        "id": "Hint-Filter",
        "type": "filters",
        "relationships": {
            "services": { // All serices that use this filter
                "links": {
                    "self": "http://localhost:8989/v1/services/"
                },
                "data": [] // No service is using this filter
            }
        },
        "attributes": {
            "module": "hintfilter",
            "parameters": {} // Filter parameters
        },
        "links": {
            "self": "http://localhost:8989/v1/filters/Hint-Filter"
        }
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
    "links": {
        "self": "http://localhost:8989/v1/filters/"
    },
    "data": [ // Array of filter resources
        {
            "id": "Hint-Filter",
            "type": "filters",
            "relationships": {
                "services": {
                    "links": {
                        "self": "http://localhost:8989/v1/services/"
                    },
                    "data": []
                }
            },
            "attributes": {
                "module": "hintfilter",
                "parameters": {}
            },
            "links": {
                "self": "http://localhost:8989/v1/filters/Hint-Filter"
            }
        }
    ]
}
```

### Create a filter

Create a new filter. The request body must define the `/data/id`
field with the name of the filter, the `/data/type` field with the
value of `filters` and the `/data/attributes/module` field with the
filter module for this filter. All of the filter parameters should
be defined at creation time.

```
POST /v1/filters
```

The following example defines a request body which creates the new filter,
_test-filter_, and assigns it to a service.

```javascript
{
    data: {
        "id": "test-filter", // Name of the filter
        "type": "filters",
        "attributes": {
            "module": "qlafilter", // The filter uses the qlafilter module
            "parameters": { // Filter parameters
                "filebase": "/tmp/qla.log"
            }
        },
        "relationships": { // List of services that use this filter
            "services": {
                "data": [ // This filter is used by one service
                    {
                        "id": "service-1",
                        "type": "services"
                    }
                ]
            }
        }
    }
}
```

#### Response

Filter is created:

`Status: 204 No Content`
