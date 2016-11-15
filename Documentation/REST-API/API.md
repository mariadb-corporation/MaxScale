# REST API design document

This document describes the version 1 of the MaxScale REST API.

## Table of Contents

- [HTTP Headers](#http-headers)
  - [Request Headers](#request-headers)
  - [Response Headers](#response-headers)
- [Response Codes](#response-codes)
  - [2xx Success](#2xx-success)
  - [3xx Redirection](#3xx-redirection)
  - [4xx Client Error](#4xx-client-error)
  - [5xx Server Error](#5xx-server-error)
- [Resources](#resources)
- [Common Request Parameter](#common-request-parameters)

## HTTP Headers

### Request Headers

REST makes use of the HTTP protocols in its aim to provide a natural way to
understand the workings of an API. The following request headers are understood
by this API.

#### Accept-Charset

Acceptable character sets.

#### Authorization

Credentials for authentication.

#### Content-Type

All PUT and POST requests must use the `Content-Type: application/json` media
type and the request body must be a valid JSON representation of a resource. All
PATCH requests must use the `Content-Type: application/json-patch` media type
and the request body must be a valid JSON Patch document which is applied to the
resource. Curently, only _add_, _remove_, _replace_ and _test_ operations are
supported.

Read the [JSON Patch](https://tools.ietf.org/html/draft-ietf-appsawg-json-patch-08)
draft for more details on how to use it with PATCH.

#### Date

This header is required and should be in the RFC 1123 standard form, e.g. Mon,
18 Nov 2013 08:14:29 -0600. Please note that the date must be in English. It
will be checked by the API for being close to the current date and time.

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
the intended method in the `X-HTTP-Method-Override` header, a client can perform
a POST, PATCH or DELETE request with the PUT method
(e.g. `X-HTTP-Method-Override: PATCH`).

_TODO: Add API version header?_

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
responding to a HEAD request, the body of the response contains a JSON
representation of the error in the following format.

```
{
    "error": "Method not supported",
    "description": "The `/service` resource does not support POST."
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

Response status codes beginning with the digit "5" indicate cases in which the
server is aware that it has encountered an error or is otherwise incapable of
performing the request. Except when responding to a HEAD request, the server
includes an entity containing an explanation of the error situation.

```
{
    "error": "Log rotation failed",
    "description": "Failed to rotate log files: 13, Permission denied"
}
```

The _error_ field contains a short error description and the _description_ field
contains a more detailed version of the error message.

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

## Resources

The MaxScale REST API provides the following resources.

- [/maxscale](Resources-MaxScale.md)
- [/services](Resources-Service.md)
- [/servers](Resources-Server.md)
- [/filters](Resources-Filter.md)
- [/monitors](Resources-Monitor.md)
- [/sessions](Resources-Session.md)
- [/users](Resources-User.md)

## Common Request Parameters

Most of the resources that support GET also support the following
parameters. See the resource documentation for a list of supported request
parameters.

- `fields`

  - A list of fields to return.

    This allows the returned object to be filtered so that only needed
    parts are returned. The value of this parameter is a comma separated
    list of fields to return.

    For example, the parameter `?fields=id,name` would return object which
    would only contain the _id_ and _name_ fields.

- `range`

  - Return a subset of the object array.

    The value of this parameter is the range of objects to return given as
    a inclusive range separated by a hyphen. If the size of the array is
    less than the end of the range, only the objects between the requested
    start of the range and the actual end of the array are returned. This
    means that

    For example, the parameter `?range=10-20` would return objects 10
    through 20 from the object array if the actual size of the original
    array is greater than or equal to 20.
