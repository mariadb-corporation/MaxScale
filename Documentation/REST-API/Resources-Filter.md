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
    data: {
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

### Destroy a filter

```
DELETE /v1/filters/:filter
```

The _:filter_ in the URI must map to the name of the filter to be destroyed.

A filter can only be destroyed if no service uses it. This means that the
`data.relationships` object for the filter must be empty. Note that the service
→ filter relationship cannot be modified from the filters resource and must be
done via the services resource.

#### Response

Filter is destroyed:

`Status: 204 No Content`
