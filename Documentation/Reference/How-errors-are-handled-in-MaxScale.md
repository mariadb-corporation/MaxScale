# How errors are handled in MaxScale

This document describes how errors are handled in MaxScale, its protocol modules and routers. 

Assume a client, maxscale, and master/slave replication cluster. 

An "error" can be due to failed authentication, routing error (unsupported query type etc.), or backend failure.

## Authentication error

Authentication is relatively complex phase in the beginning of session creation. Roughly speaking, client protocol has loaded user information from backend so that it can authenticate client without consulting backend. When client sends authentication data to MaxScale data is compared against backend’s user data in the client protocol module. If authentication fails client protocol module refreshes backend data just in case it had became obsolete after last refresh. If authentication still fails after refresh, authentication error occurs.

Close sequence starts from mysql_client.c:gw_read_client_event where

1. session state is set to SESSION_STATE_STOPPING

2. dcb_close is called for client DCB

    1. client DCB is removed from epoll set and state is set to DCB_STATE_NOPOLLING

    2. client protocol’s close is called (gw_client_close)

        * protocol struct is done’d

        * router’s closeSession is called (includes calling dcb_close for backends)

    3. dcb_call_callback is called for client DCB with DCB_REASON_CLOSE

    4. client DCB is set to zombies list

Each call for dcb_close in closeSession repeat steps 2a-d.

## Routing errors

### Invalid capabilities returned by router

When client protocol module receives query from client the protocol state is (typically) MYSQL_IDLE. The protocol state is checked in mysql_client.c:gw_read_client_event. First place where a hard error may occur is when router capabilities are read. If router response is invalid (other than RCAP_TYPE_PACKET_INPUT and RCAP_TYPE_STMT_INPUT). In case of invalid return value from the router, error is logged, followed by session closing.

### Backend failure

When mysql_client.c:gw_read_client_event calls either route_by_statement or directly SESSION_ROUTE_QUERY script, which calls the routeQuery function of the head session’s router. routeQuery returns 1 if succeed, or 0 in case of error. Success here means that query was routed and reply will be sent to the client while error means that routing failed because of backend (server/servers/service) failure or because of side effect of backend failure. 

In case of backend failure, error is replied to client and handleError is called to resolve backend problem. handleError is called with action ERRACT_NEW_CONNECTION which tells to error handler that it should try to find a replacement for failed backend. Handler will return true if there are enough backend servers for session’s needs. If handler returns false it means that session can’t continue processing further queries and will be closed. Client will be sent an error message and dcb_close is called for client DCB.

Close sequence is similar to that described above from phase #2 onward.

Reasons for "backend failure" in rwsplit:

* router has rses_closed == true because other thread has detected failure and started to close session

* master has disappeared; demoted to slave, for example

### Router error

In cases where SESSION_ROUTE_QUERY has returned successfully (=1) query may not be successfully processed in backend or even sent to it. It is possible that router fails in routing the particular query but there is no such error which would prevent session from continuing. In this case router handles error silently by creating and adding MySQL error to first available backend’s (incoming) eventqueue where it is found and sent to client (clientReply).

