# REST API design document

This document describes the version 1 of the MaxScale REST API.

## Table of Contents

- [Resources](#resources)
- [Common Request Parameter](#common-request-parameters)
- [HTTP Headers](#http-headers)
  - [Request Headers](#request-headers)
  - [Response Headers](#response-headers)
- [Response Codes](#response-codes)
  - [2xx Success](#2xx-success)
  - [3xx Redirection](#3xx-redirection)
  - [4xx Client Error](#4xx-client-error)
  - [5xx Server Error](#5xx-server-error)

## Note About Syntax

Although JSON does not define a syntax for comments, some of the JSON examples
have C-style inline comments in them. These comments use `//` to mark the start
of the comment and extend to the end of the current line.

## Authentication

The MaxScale REST API uses [HTTP Basic Access](https://tools.ietf.org/html/rfc2617#section-2)
authentication with the MaxScale administrative interface users. The default
user is `admin:mariadb`, the same as the MaxAdmin network user.

It is highly recommended to enable HTTPS on the MaxScale REST API to make the
communication between the client and MaxScale secure. Without it, the passwords
can be intercepted from the network traffic. Refer to the
[Configuration Guide](../Getting-Started/Configuration-Guide.md#admin_ssl_key) for more
details on how to enable HTTPS for the MaxScale REST API.

For more details on how administrative interface users are created and managed,
refer to the [MaxAdmin](../Reference/MaxAdmin.md) documentation as well as the
documentation of the [users](Resources-User.md) resource.

## Resources

The MaxScale REST API provides the following resources. All resources conform to
the [JSON API](http://jsonapi.org/format/) specification.

- [maxscale](Resources-MaxScale.md)
- [services](Resources-Service.md)
- [servers](Resources-Server.md)
- [filters](Resources-Filter.md)
- [monitors](Resources-Monitor.md)
- [sessions](Resources-Session.md)
- [users](Resources-User.md)

### Resource Relationships

All resources return complete JSON objects. The returned objects can have a
_relationships_ field that represents any relations the object has to other
objects. This closely resembles the JSON API definition of links.

In the _relationships_ objects, all resources have a _self_ link that points to
the resource itself. This allows for easier updating of resources as the reply
URL is included in the response itself.

The following lists the resources and the types of links each resource can have
in addition to the _self_ link.

- `services` - Service resource

  - `servers`

    List of servers used by the service

  - `filters`

    List of filters used by the service

- `monitors` - Monitor resource

  - `servers`

    List of servers used by the monitor

- `filters` - Filter resource

  - `services`

    List of services that use this filter

- `servers` - Server resource

  - `services`

    List of services that use this server

  - `monitors`

    List of monitors that use this server

## Common Request Parameters

All the resources that return JSON content also support the following
parameters.

- `pretty`

  - Pretty-print output.

    If this parameter is set to `true` then the returned objects are
    formatted in a more human readable format. All resources support this
    parameter.

## HTTP Headers

### Request Headers

REST makes use of the HTTP protocols in its aim to provide a natural way to
understand the workings of an API. The following request headers are understood
by this API.

#### Accept-Charset

Acceptable character sets.

#### Authorization

Credentials for authentication. This header should consist of a HTTP Basic
Access authentication type payload which is the base64 encoded value of the
username and password joined by a colon e.g. `Base64("maxuser:maxpwd")`. The
REST API uses the same users as the MaxAdmin interface. For more details about
MaxScale administrative users, refer to the [MaxAdmin](../Reference/MaxAdmin.md)
documentation.

#### Content-Type

All PUT and POST requests must use the `Content-Type: application/json` media
type and the request body must be a complete and valid JSON representation of a
resource. All PATCH requests must use the `Content-Type: application/json` media
type and the request body must be a JSON document containing a partial
definition of the original resource.

The current version of the API supports PATCH-like PUT requests with
partial definitions of resources in the request body. This is discouraged
as it goes against the intended use of the PUT method. Future versions of
the MaxScale REST API can remove this support which means that this
functionality is deprecated.

#### Host

The address and port of the server.

#### If-Match

The request is performed only if the provided ETag value matches the one on the
server. This field should be used with PUT requests to prevent concurrent
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
example the resource `/service` will always return the `Accept: GET, PATCH, PUT`
header.

#### Accept-Patch

All PATCH capable resources return the `Accept-Patch: application/json-patch`
header.

#### Date

Returns the RFC 1123 standard form date when the reply was sent. The date is in
English and it uses the server's local timezone.

#### ETag

An identifier for a specific version of a resource. The value of this header
changes whenever a resource is modified.

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
        "detail" : "The new `/server/` resource is missing the `port` parameter"
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
