# Lua Filter

The luafilter is a filter that calls a set of functions in a Lua script. The
filter is currently a part of the experimental module set.

Read the [Lua language documentation](https://www.lua.org/docs.html) for
information on how to write Lua scripts.

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

  - `nil createInstance()` - global script only, called when MaxScale is started

    - The global script will be loaded in this function and executed once on a
      global level before calling the createInstance function in the Lua script.

  - `nil newSession()` - new session is created

    - This function first loads the session script and executes in on a global
      level.  After this, the newSession function in the Lua scripts is called.
      There is a single C function exported as a global variable for the session
      script named id_gen. The id_gen function returns an integer that is unique
      for this service only. This function is only accessible to the session
      level scripts.

  - `nil closeSession()` - session is closed

    - The `closeSession` function in the Lua scripts will be called.

  - `(nil | bool | string) routeQuery(string)` - query is being routed
  
    - The Luafilter calls the `routeQuery` functions of both the session and the
      global script.  The query is passed as a string parameter to the
      routeQuery Lua function and the return values of the session specific
      function, if any were returned, are interpreted. If the first value is
      bool, it is interpreted as a decision whether to route the query or to
      send an error packet to the client.  If it is a string, the current query
      is replaced with the return value and the query will be routed. If nil is
      returned, the query is routed normally.

  - `nil clientReply()` - reply to a query is being routed

    - This function calls the `clientReply` function of the Lua scripts.

  - `string diagnostic()` - global script only, print diagnostic information

    - This will call the matching `diagnostics` entry point in the Lua script. If
      the Lua function returns a string, it will be printed to the client.

These functions, if found in the script, will be called whenever a call to the
matching entry point is made.
