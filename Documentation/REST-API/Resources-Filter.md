# Filter Resource

A filter resource represents an instance of a filter inside MaxScale. Multiple
services can use the same filter and a single service can use multiple filters.

## Resource Operations

### Get a filter

Get a single filter. The _:name_ in the URI must be a valid filter name with all
whitespace replaced with hyphens. The filter names are case-insensitive.

```
GET /filters/:name
```

#### Response

```
Status: 200 OK

{
    "name": "Query Logging Filter",
    "module": "qlafilter",
    "parameters": {
        "filebase": {
            "value": "/var/log/maxscale/qla/log.",
            "configurable": false
        },
        "match": {
            "value": "select.*from.*t1",
            "configurable": true
        }
    },
    "services": [
        "/services/my-service",
        "/services/my-second-service"
    ]
}
```

#### Supported Request Parameter

- `fields`

### Get all filters

Get all filters.

```
GET /filters
```

#### Response

```
Status: 200 OK

[
    {
        "name": "Query Logging Filter",
        "module": "qlafilter",
        "parameters": {
            "filebase": {
                "value": "/var/log/maxscale/qla/log.",
                "configurable": false
            },
            "match": {
                "value": "select.*from.*t1",
                "configurable": true
            }
        },
        "services": [
            "/services/my-service",
            "/services/my-second-service
        ]
    },
    {
        "name": "DBFW Filter",
        "module": "dbfwfilter",
        "parameters": {
            {
                "name": "rules",
                "value": "/etc/maxscale-rules",
                "configurable": false
            }
        },
        "services": [
            "/services/my-second-service
        ]
    }
]
```

#### Supported Request Parameter

- `fields`
- `range`

### Update a filter

**Note**: The update mechanisms described here are provisional and most likely
  will change in the future. This description is only for design purposes and
  does not yet work.

Partially update a filter. The _:name_ in the URI must map to a filter name
and the request body must be a valid JSON Patch document which is applied to the
resource.

```
PATCH /filter/:name
```

### Modifiable Fields

|Field       |Type   |Description                      |
|------------|-------|---------------------------------|
|parameters  |object |Module specific filter parameters|

```
[
    { "op": "replace", "path": "/parameters/rules/value", "value": "/etc/new-rules" },
    { "op": "add", "path": "/parameters/action/value", "value": "allow" }
]
```

#### Response

Response contains the modified resource.

```
Status: 200 OK

{
    "name": "DBFW Filter",
    "module": "dbfwfilter",
    "parameters": {
        "rules": {
            "value": "/etc/new-rules",
            "configurable": false
        },
        "action": {
            "value": "allow",
            "configurable": true
        }
    }
    "services": [
        "/services/my-second-service"
    ]
}
```
