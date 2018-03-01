# Admin User Resource

Admin users represent administrative users that are able to query and change
MaxScale's configuration.

## Resource Operations

### Get all users

Get all administrative users.

```
GET /users
```

#### Response

```
Status: 200 OK

[
    {
        "name": "jdoe"
    },
    {
        "name": "dba"
    },
    {
        "name": "admin"
    }
]

#### Supported Request Parameter

- `fields`
- `range`

### Create a user

Create a new administrative user.

```
PUT /users
```

### Modifiable Fields

All of the following fields need to be defined in the request body.

|Field    |Type  |Description              |
|---------|------|-------------------------|
|name     |string|Username, consisting of alphanumeric characters|
|password |string|Password for the new user|

```
{
    "name": "foo",
    "password": "bar"
}
```

#### Response

```
Status: 204 No Content
```

### Delete a user

Delete a user. The _:name_ part of the URI must be a valid user name. The user
names are case-insensitive.

```
DELETE /users/:name
```

#### Response

```
Status: 204 No Content
```
