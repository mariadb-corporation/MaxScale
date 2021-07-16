# REST API

This document describes the version 1 of the MaxScale REST API.

[TOC]

## Note About Syntax

Although JSON does not define a syntax for comments, some of the JSON examples
have C-style inline comments in them. These comments use `//` to mark the start
of the comment and extend to the end of the current line.

## Configuration

Read the [REST API](../Getting-Started/Configuration-Guide.md#rest-api-configuration)
section of the configuration guide for more details on how to configure the REST API.

## Authentication

The MaxScale REST API uses [HTTP Basic Access](https://tools.ietf.org/html/rfc2617#section-2)
authentication with the MaxScale administrative interface users. The default
user is `admin:mariadb`.

It is highly recommended to enable HTTPS on the MaxScale REST API to make the
communication between the client and MaxScale secure. Without it, the passwords
can be intercepted from the network traffic. Refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md#admin_ssl_key) for more
details on how to enable HTTPS for the MaxScale REST API.

For more details on how administrative interface users are created and managed,
refer to the [MaxCtrl](../Reference/MaxCtrl.md) documentation as well as the
documentation of the [users](Resources-User.md) resource.

### JSON Web Tokens

MaxScale supports authentication via
[JSON Web Tokens](https://tools.ietf.org/html/rfc7519).

```
GET /v1/auth
```

The `/v1/auth` endpoint can be used to generate new tokens which are returned in
the following form.

```javascript
{
    "meta": {
        "token": "eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJhZG1pbiIsImV4cCI6MTU4MzI1NDE1MSwiaWF0IjoxNTgzMjI1MzUxLCJpc3MiOiJtYXhzY2FsZSJ9.B1BqhjjKaCWKe3gVXLszpOPfeu8cLiwSb4CMIJAoyqw"
    }
}
```

Note that by default the `/auth` endpoint requires the connection to be
encrypted (HTTPS) and attempts to use it without encryption will be treated as
an error. To allow use of the `/auth` endpoint without encryption, use
`admin_secure_gui=false`.

If the token is used to authenticate users in a web browser, the token can be
optionally stored in cookies. This can be enabled with the `persist=yes`
parameter in the request:

```
GET /v1/auth?persist=yes
```

When the token is stored in the cookies, it will be split into two parts: the
JWT body will be stored in a cookie named `token_body` and the JWT signature is
stored in `token_sig`. The JWT signature will be stored with `SameSite=Strict`
and `HttpOnly` cookie options which means the JavaScript context of the browser
will not have access to it. This is done to prevent CSRF attacks.

By default, the generated tokens are valid for 8 hours. The token validity
period can be set with the `max-age` request parameter:

```
GET /v1/auth?max-age=28800
```

When `max-age` is combined with `persist`, the `Max-Age` cookie option is
also set to the same value.

To use the token for authentication, the generated token must be presented in
the Authorization header with the Bearer authentication scheme. For example, the
token above would be used in the following manner:

```
Authorization: Bearer eyJhbGciOiJIUzI1NiJ9.eyJhdWQiOiJhZG1pbiIsImV4cCI6MTU4MzI1NDE1MSwiaWF0IjoxNTgzMjI1MzUxLCJpc3MiOiJtYXhzY2FsZSJ9.B1BqhjjKaCWKe3gVXLszpOPfeu8cLiwSb4CMIJAoyqw
```

If MaxScale is restarted, all generated tokens are invalidated.

#### `/auth` Request Parameters

The `/auth` endpoint supports the following request parameters that must be
given in the HTTP query string.

- `max-age`

  - Sets the token maximum age in seconds. The default is `max-age=28800`. Only
    positive values are accepted and if a non-positive or a non-integer value is
    found, the parameter is ignored.

- `persist`

  - Store the generated token in cookies instead of returning it as the response body.

    This parameter expects only one value, `yes`, as its argument. When
    `persist=yes` is set, the token is stored in two cookies, `token_body` and
    `token_sig`, and the response is 204 No Content instead of 200 OK.

    The `token_body` cookie contains the JWT header and claims sections
    (i.e. the token body before the second period). This can be accessed by
    JavaScript.

    The `token_sig` part contains the rest of the token. The cookie is stored as
    a HttpOnly cookie which prevents access to from JavaScript. This is done to
    mitigate any attacks that might leak the token.

## Resources

The MaxScale REST API provides the following resources. All resources conform to
the [JSON API](http://jsonapi.org/format/) specification.

- [maxscale](Resources-MaxScale.md)
- [services](Resources-Service.md)
- [servers](Resources-Server.md)
- [listeners](Resources-Listener.md)
- [filters](Resources-Filter.md)
- [monitors](Resources-Monitor.md)
- [sessions](Resources-Session.md)
- [users](Resources-User.md)

In addition to the named resources, the REST API will respond with a HTTP 200 OK
response to GET requests on the root resource (`/`) as well as the namespace
root resource (`/v1/`). These can be used for HTTP health checks to determine
whether MaxScale is running.

## API Versioning

All of the current resources are in the `/v1/` namespace of the MaxScale REST
API. Further additions to the namespace can be added that do not break backwards
compatibility of any existing resources. What this means in practice is that:

* No resources or URLs will be removed
* The API will be JSON API compliant

Note that this means that the contents of individual resources can change. New
fields can be added, old ones can be removed and the meaning of existing fields
can change. The aim is to be as backwards compatible as reasonably possible
without sacrificing the clarity and functionality of the API.

Since MaxScale 2.4.0, the use of the version prefix `/v1/` is optional: if the
prefix is not used, the latest API version is used.

### Resource Relationships

All resources return complete JSON objects. The returned objects can have a
_relationships_ field that represents any relations the object has to other
objects. This closely resembles the JSON API definition of links.

In the _relationships_ objects, all resources have a _self_ link that points to
the resource itself. This allows easy access to the objects pointed by the
relationships as the reply URL is included in the response itself.

To create a relationship between two objects, define it in the initial POST
request. To modify the relationships of existing objects, perform a PATCH
request with the new definition of the relevant relationship. To completely
remove all relationships from an object, the `data` field of the corresponding
relationship object must be set to an empty array.

The following lists the resources and the types of links each resource can have
in addition to the _self_ link. Examples of these relationships can be seen in
the resource documentation.

- `services` - Service resource

  - `servers`

    List of servers used by the service

  - `services`

    List of services used by the service

  - `filters`

    List of filters used by the service

    **NOTE:** This is an ordered relationship where the order of the filters
      defines the order in which they process queries.

  - `listeners`

    List of listeners used by the service

- `monitors` - Monitor resource

  - `servers`

    List of servers used by the monitor

- `filters` - Filter resource

  - `services`

    List of services that use this filter

    **NOTE:** This is a one-way relationship that can only be modified from the
    `services` resource.

- `servers` - Server resource

  - `services`

    List of services that use this server

  - `monitors`

    List of monitors that use this server

- `listeners` - Listener resource

  - `services`

    The service that the listener points to

## Common Request Parameters

All the resources that return JSON content also support the following
parameters. Parameters are given in the HTTP query string:
`https://localhost:8989/v1/servers?pretty=true&fields[servers]=state`.

- `pretty`

  - Pretty-print output.

    If this parameter is set to `true` then the returned objects are formatted
    in a more human readable format. If the parameter is set to `false` then the
    returned objects are formatted in a compact format.  All resources support
    this parameter. The default value for this parameter is `true`.

- `fields[TYPE]=field1,field2...`

  - Return a [Sparse Fieldset](https://jsonapi.org/format/#fetching-sparse-fieldsets)

    This parameter controls which fields are returned in the REST API
    response. The `TYPE` value in the `fields` parameter must be the resource
    type that is being retrieved (i.e. the `servers` in `/v1/servers` and
    `/v1/server/server1`). The value of the parameter must be a comma-separated
    list of [JSON Pointers](https://tools.ietf.org/html/rfc6901) that mark which
    fields of the object to return. Only fields in objects in the `attributes`
    and `relationships` objects are inspected. This means that if the path
    marked by the JSON Pointer contains an array in it, it will not advance past
    this array.

    For example, to return only the server state output from the `/servers`
    endpoint, the `fields[servers]=state` parameter can be used. This would
    return only the `data.attributes.state` part of the resource. To return the
    nested value `data.attributes.statistics.connections`, the corresponding
    parameter would be `fields[servers]=statistics/connections`.

- `filter=json_ptr=json_value`

  - Filter the output of the result

    This parameter controls which rows are returned in a REST API response that
    returns an array in the `data` member (i.e. a request to a resource
    collection). Requests to individual resources are not filtered.

    The argument to the filter parameter must be a key-value pair with a valid
    [JSON Pointer](https://tools.ietf.org/html/rfc6901) as the key and a valid
    JSON type as the value. The comparison is done for each individual object in
    the `data` array of the result. For example, if the object stored in
    `data[0]` has a value pointed by the given JSON pointer and that value
    compares equal to the given value, the array row is kept in the result.

    A practical use for this parameter is to return only sessions for a
    particular service. For example, to return sessions for the
    `RW-Split-Router` service, the
    `filter=/relationships/services/data/0/id="RW-Split-Router"` parameter can
    be used. Note the double quotes around the `"RW-Split-Router"`, they are
    required to correctly convert strings into JSON values.

## HTTP Headers

### Request Headers

REST makes use of the HTTP protocols in its aim to provide a natural way to
understand the workings of an API. The following request headers are understood
by this API.

#### Authorization

Credentials for authentication. This header should consist of a HTTP Basic
Access authentication type payload which is the base64 encoded value of the
username and password joined by a colon e.g. `Base64("maxuser:maxpwd")`.

#### Content-Type

All PUT and POST requests must use the `Content-Type: application/json` media
type and the request body must be a complete and valid JSON representation of a
resource. All PATCH requests must use the `Content-Type: application/json` media
type and the request body must be a JSON document containing a partial
definition of the modified resource.

#### Host

The address and port of the server.

#### If-Match

The request is performed only if the provided ETag value matches the one on the
server. This field should be used with PATCH requests to prevent concurrent
updates to the same resource.

The value of this header must be a value from the `ETag` header retrieved from
the same resource at an earlier point in time.

#### If-Modified-Since

If the content has not changed the server responds with a 304 status code.  If
the content has changed the server responds with a 200 status code and the
requested resource.

The value of this header must be a date value in the
["HTTP-date"](https://www.ietf.org/rfc/rfc2822.txt) format.

#### If-None-Match

If the content has not changed the server responds with a 304 status code.  If
the content has changed the server responds with a 200 status code and the
requested resource.

The value of this header must be a value from the `ETag` header retrieved from
the same resource at an earlier point in time.

#### If-Unmodified-Since

The request is performed only if the requested resource has not been modified
since the provided date.

The value of this header must be a date value in the
["HTTP-date"](https://www.ietf.org/rfc/rfc2822.txt) format.

#### X-HTTP-Method-Override

Some clients only support GET and PUT requests. By providing the string value of
the intended method in the `X-HTTP-Method-Override` header, a client can, for
example, perform a POST, PATCH or DELETE request with the PUT method
(e.g. `X-HTTP-Method-Override: PATCH`).

If this header is defined in the request, the current method of the request is
replaced with the one in the header. The HTTP method must be in uppercase and it
must be one of the methods that the requested resource supports.

### Response Headers

#### Allow

All resources return the Allow header with the supported HTTP methods. For
example the resource `/services` will always return the `Accept: GET, PATCH, PUT`
header.

#### Accept-Patch

All PATCH capable resources return the `Accept-Patch: application/json-patch`
header.

#### Date

Returns the RFC 1123 standard form date when the reply was sent. The date is in
English and it uses the server's local timezone.

#### ETag

An identifier for a specific version of a resource. The value of this header
changes whenever a resource is modified via the REST API. It will not change if
an internal MaxScale event (e.g. server changing state or statistics being
updated) causes a change.

When the client sends the `If-Match` or `If-None-Match` header, the provided
value should be the value of the `ETag` header of an earlier GET.

#### Last-Modified

The date when the resource was last modified in "HTTP-date" format.

#### Location

If an out of date resource location is requested, a HTTP return code of 3XX with
the `Location` header is returned. The value of the header contains the new
location of the requested resource as a relative URI.

#### WWW-Authenticate

The requested authentication method. For example, `WWW-Authenticate: Basic`
would require basic HTTP authentication.

## Response Codes

Every HTTP response starts with a line with a return code which indicates the
outcome of the request. The API uses some of the standard HTTP values:

### 2xx Success

- 200 OK

  - Successful HTTP requests, response has a body.

- 201 Created

  - A new resource was created.

- 202 Accepted

  - The request has been accepted for processing, but the processing has not
    been completed.

- 204 No Content

  - Successful HTTP requests, response has no body.

### 3xx Redirection

This class of status code indicates the client must take additional action to
complete the request.

- 301 Moved Permanently

  - This and all future requests should be directed to the given URI.

- 302 Found

  - The response to the request can be found under another URI using the same
    method as in the original request.

- 303 See Other

  - The response to the request can be found under another URI using a GET
    method.

- 304 Not Modified

  - Indicates that the resource has not been modified since the version
    specified by the request headers If-Modified-Since or If-None-Match.

- 307 Temporary Redirect

  - The request should be repeated with another URI but future requests should
    use the original URI.

- 308 Permanent Redirect

  - The request and all future requests should be repeated using another URI.

### 4xx Client Error

The 4xx class of status code is when the client seems to have erred. Except when
responding to a HEAD request, the body of the response *MAY* contains a JSON
representation of the error.

```javascript
{
    "error": {
        "detail" : "The new `/servers/` resource is missing the `port` parameter"
    }
}
```

The _error_ field contains a short error description and the _description_ field
contains a more detailed version of the error message.

- 400 Bad Request

  - The server cannot or will not process the request due to client error.

- 401 Unauthorized

  - Authentication is required. The response includes a WWW-Authenticate header.

- 403 Forbidden

  - The request was a valid request, but the client does not have the necessary
    permissions for the resource.

- 404 Not Found

  - The requested resource could not be found.

- 405 Method Not Allowed

  - A request method is not supported for the requested resource.

- 406 Not Acceptable

  - The requested resource is capable of generating only content not acceptable
    according to the Accept headers sent in the request.

- 409 Conflict

  - Indicates that the request could not be processed because of conflict in the
    request, such as an edit conflict be tween multiple simultaneous updates.

- 411 Length Required

  - The request did not specify the length of its content, which is required by
    the requested resource.

- 412 Precondition Failed

  - The server does not meet one of the preconditions that the requester put on
    the request.

- 413 Payload Too Large

  - The request is larger than the server is willing or able to process.

- 414 URI Too Long

  - The URI provided was too long for the server to process.

- 415 Unsupported Media Type

  - The request entity has a media type which the server or resource does not
    support.

- 422 Unprocessable Entity

  - The request was well-formed but was unable to be followed due to semantic
    errors.

- 423 Locked

  - The resource that is being accessed is locked.

- 428 Precondition Required

  - The origin server requires the request to be conditional. This error code is
    returned when none of the `Modified-Since` or `Match` type headers are used.

- 431 Request Header Fields Too Large

  - The server is unwilling to process the request because either an individual
    header field, or all the header fields collectively, are too large.

### 5xx Server Error

The server failed to fulfill an apparently valid request.

- 500 Internal Server Error

  - A generic error message, given when an unexpected condition was encountered
    and no more specific message is suitable.

- 501 Not Implemented

  - The server either does not recognize the request method, or it lacks the
    ability to fulfill the request.

- 502 Bad Gateway

  - The server was acting as a gateway or proxy and received an invalid response
    from the upstream server.

- 503 Service Unavailable

  - The server is currently unavailable (because it is overloaded or down for
    maintenance). Generally, this is a temporary state.

- 504 Gateway Timeout

  - The server was acting as a gateway or proxy and did not receive a timely
    response from the upstream server.

- 505 HTTP Version Not Supported

  - The server does not support the HTTP protocol version used in the request.

- 506 Variant Also Negotiates

  - Transparent content negotiation for the request results in a circular
    reference.

- 507 Insufficient Storage

  - The server is unable to store the representation needed to complete the
    request.

- 508 Loop Detected

  - The server detected an infinite loop while processing the request (sent in
    lieu of 208 Already Reported).

- 510 Not Extended

  - Further extensions to the request are required for the server to fulfil it.

### Response Headers Reserved for Future Use

The following response headers are not currently in use. Future versions of the
API could return them.

- 206 Partial Content

  - The server is delivering only part of the resource (byte serving) due to a
    range header sent by the client.

- 300 Multiple Choices

  - Indicates multiple options for the resource from which the client may choose
    (via agent-driven content negotiation).

- 407 Proxy Authentication Required

  - The client must first authenticate itself with the proxy.

- 408 Request Timeout

  - The server timed out waiting for the request. According to HTTP
    specifications: "The client did not produce a request within the time that
    the server was prepared to wait. The client MAY repeat the request without
    modifications at any later time."

- 410 Gone

  - Indicates that the resource requested is no longer available and will not be
    available again.

- 416 Range Not Satisfiable

  - The client has asked for a portion of the file (byte serving), but the
    server cannot supply that portion.

- 417 Expectation Failed

  - The server cannot meet the requirements of the Expect request-header field.

- 421 Misdirected Request

  - The request was directed at a server that is not able to produce a response.

- 424 Failed Dependency

  - The request failed due to failure of a previous request.

- 426 Upgrade Required

  - The client should switch to a different protocol such as TLS/1.0, given in
    the Upgrade header field.

- 429 Too Many Requests

  - The user has sent too many requests in a given amount of time. Intended for
    use with rate-limiting schemes.
