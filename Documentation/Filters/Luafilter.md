# Lua Filter

The luafilter is a filter that calls a set of functions in a Lua script.

Read the [Lua language documentation](https://www.lua.org/docs.html) for
information on how to write Lua scripts.

*Note:* This module is experimental and must be built from source. The
 module is deprecated in MaxScale 23.08 and might be removed in a future
 release.

[TOC]

## Filter Parameters

The luafilter has two parameters. They control which scripts will be called by
the filter. Both parameters are optional but at least one should be defined. If
both `global_script` and `session_script` are defined, the entry points in both
scripts will be called.

### `global_script`

The global Lua script. The parameter value is a path to a readable Lua script
which will be executed.

This script will always be called with the same global Lua state and it can be
used to build a global view of the whole service.

### `session_script`

The session level Lua script. The parameter value is a path to a readable Lua
script which will be executed once for each session.

Each session will have its own Lua state meaning that each session can have a
unique Lua environment. Use this script to do session specific tasks.

## Lua Script Calling Convention

The entry points for the Lua script expect the following signatures:

  - `nil createInstance(name)` - global script only, called when the script is first loaded

    - When the global script is loaded, it first executes on a global level
      before the luafilter calls the createInstance function in the Lua script
      with the filter's name as its argument.

  - `nil newSession(string, string)` - new session is created

    - After the session script is loaded, the newSession function in the Lua
      scripts is called. The first parameter is the username of the client and
      the second parameter is the client's network address.

  - `nil closeSession()` - session is closed

    - The `closeSession` function in the Lua scripts will be called.

  - `(nil | bool | string) routeQuery()` - query is being routed

    - The Luafilter calls the `routeQuery` functions of both the session and the
      global script.  The query is passed as a string parameter to the
      routeQuery Lua function and the return values of the session specific
      function, if any were returned, are interpreted. If the first value is
      bool, it is interpreted as a decision whether to route the query or to
      send an error packet to the client.  If it is a string, the current query
      is replaced with the return value and the query will be routed. If nil is
      returned, the query is routed normally.

  - `nil clientReply()` - reply to a query is being routed

    - This function is called with the name of the server that returned the response.

  - `string diagnostic()` - global script only, print diagnostic information

    - If the Lua function returns a string that is valid JSON, it will be
      decoded as JSON and displayed as such in the REST API. If the object does
      not decode into JSON, it will be stored as a JSON string.

These functions, if found in the script, will be called whenever a call to the
matching entry point is made.

#### Script Template

Here is a script template that can be used to try out the luafilter. Copy it
into a file and add `global_script=<path to script>` into the filter
configuration. Make sure the file is readable by the `maxscale` user.

```
function createInstance(name)

end

function newSession(user, host)

end

function closeSession()

end

function routeQuery()

end

function clientReply()

end

function diagnostic()

end
```

### Functions Exposed by the Luafilter

The luafilter exposes the following functions that can be called inside the Lua
script API endpoints. The callback function in which they can be called is
documented after the function signature. If the functions are called outside of
the correct callback function, they raise a Lua error.

- `string mxs_get_sql()` (use: `routeQuery`)

  - Returns the SQL of the query being executed. This returns an empty string
    for any query that is not a text protocol query (COM_QUERY). Support for
    prepared statements is not yet implemented.

- `string mxs_get_type_mask()` (use: `routeQuery`)

  - Returns the type of the current query being executed as a string. The values
    are the string versions of the query types defined in _query_classifier.h_
    are separated by vertical bars (`|`).

    This function can only be called from the `routeQuery` entry point.

- `string mxs_get_operation()` (use: `routeQuery`)

  - Returns the current operation type as a string. The values are defined in
    _query_classifier.h_.

    This function can only be called from the `routeQuery` entry point.

- `string mxs_get_canonical()` (use: `routeQuery`)

  - Returns the canonical version of a query by replacing all user-defined constant values with question marks.

    This function can only be called from the `routeQuery` entry point.

- `number mxs_get_session_id()` (use: `newSession`, `routeQuery`, `clientReply`, `closeSession`)

  - This function returns the session ID of the current session. Inside the
    `createInstance` and `diagnostic` endpoints this function will always return
    the value 0.

- `string mxs_get_db()` (use: `newSession`, `routeQuery`, `clientReply`, `closeSession`)

  - Returns the current default database used by the connection.

- `string mxs_get_user()` (use: `newSession`, `routeQuery`, `clientReply`, `closeSession`)

  - Returns the username of the client connection.

- `string mxs_get_host()` (use: `newSession`, `routeQuery`, `clientReply`, `closeSession`)

  - Returns the address of the client connection.

- `string mxs_get_replier()` (use: `clientReply`)

  - Returns the target that returned the result to the latest query.

## Example Configuration and Script

Here is a minimal configuration entry for a luafilter definition.

```
[MyLuaFilter]
type=filter
module=luafilter
global_script=/path/to/script.lua
```

And here is a script that opens a file in `/tmp/` and logs output to it.

```
f = io.open("/tmp/test.log", "a+")

function createInstance(name)
    f:write("createInstance for " .. name .. "\n")
end

function newSession(user, host)
    f:write("newSession for: " .. user .. "@" .. host .. "\n")
end

function closeSession()
    f:write("closeSession\n")
end

function routeQuery()
    f:write("routeQuery: " .. mxs_get_sql() .. " -- type: " .. mxs_qc_get_type_mask() .. " operation: " .. mxs_qc_get_operation() .. "\n")
end

function clientReply()
    f:write("clientReply: " .. mxs_get_replier() .. "\n")
end

function diagnostic()
    f:write("diagnostics\n")
    return "Hello from Lua!"
end

```

## Limitations

* `mxs_get_sql()` and `mxs_get_canonical()` do not work with queries done with
  the binary protocol.

* The Lua code is not restricted in any way which means excessively slow
  execution of it can cause the MaxScale process to become slower or to be
  aborted due to a SystemD watchdog timeout.