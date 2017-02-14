# Custom plugin development manual for MariaDB MaxScale

This document explains prospective plugin developers the MariaDB MaxScale plugin API and how the core program expects the plugins to work. It will also lay out some best practices and possible pitfalls in module development. We predict that *filters* and *routers* are the module types outside developers are most likely to work on, so the APIs of these two are discussed in detail. The other module types are only glossed over in the current version of this document.

This file should be included with the plugin development package.

## Introduction

MariaDB MaxScale is designed to be an extensible program, and as a result is something of a "swiss army-knife" of SQL-proxies. Much, if not most, of the actual processing is done by plugin modules. Plugins receive network data, process it and relay it to its destination. The MaxScale core loads plugins, manages client sessions and threads and, most importantly, offers a selection of functions for the plugins to call upon. This collection of functions is called the *Core Public Interface* or just *CPI* for short. (**or something else??**).

The plugin modules are shared libraries (.so-files) implementing a set of interface functions, the API. Different plugin types have different APIs, although there are similarities. The CPI is a set of C and C++ header files, from which the module code includes the ones required. MariaDB MaxScale is written in C/C++ and the plugin interface is in pure C. Although it is possible to write plugins in any language capable of exposing a C interface and dynamically binding to the core program, in this document we assume plugin modules are written in C++.

The result of compiling a plugin should be a single shared library file. MariaDB MaxScale expects the filename to be `lib<name>.so`, where `<name>` must match the module name given in the configuration file.

## Module categories

This section lists all the module types and summarises their core tasks. The modules are listed in the order a client packet would typically travel through. For more information about a particular module type, see the corresponding folder in `MaxScale/Documentation/`.

### Protocol

Protocol modules implement I/O between clients and MaxScale, and between MaxScale and backend servers. Protocol modules read and write to socket descriptors using *raw* I/O functions provided by the CPI, and expose protocol-specific I/O functions for other modules to use. The Protocol module API is defined in `protocol.h`.

Currently, the only implemented database protocol is *mySQL*. Other protocols currently in use include *HTTPD* and *maxscaled*-protocols, which are used by the MaxInfo and MaxAdmin modules.

### Authenticator

Authenticator modules retrieve user account information from the backend databases, store it and use it to authenticate connecting clients. MariaDB MaxScale includes authenticators for MySQL (normal and GSSApi). The authenticator API is defined in `authenticator.h`.

### Filter

Filter modules process data from client before routing. A data buffer may travel through multiple filters before arriving in a router. For a data buffer going from a backend to the client, the router receives it first and the filters receive it in reverse order. MaxScale includes a healthly selection of filters ranging from logging, overwriting query data and caching. The filter API is defined in `filter.h`.

### Router

Router modules route packets from the last filter in the filter chain to backends and reply data from backends to the last filter. The routing decisions may be based on a variety of conditions; typically packet contents and backend status are the most significant factors. Router are often used for load balancing, dividing clients and even individual queries between backends. Routers use protocol functions to write to backends, making them somewhat protocol-agnostic. The router API is defined in `router.h`.    

### Monitor

Monitor modules do not process data flowing through MariaDB MaxScale, but support the other modules (**OR JUST routers?**) in their operation by updating the status of the backend servers. Monitors are ran in their own threads to minimize interference. They periodically connect to all their assigned backends, query their status and write the results in global structs. The monitor API is defined in `monitor.h`

### A note on module dependencies

In the ideal case modules other than the protocol modules themselves should not be protocol-speficic. This does not apply in reality, since many actions in the modules are dependent on protocol-speficic details. In the future, protocol modules may be expanded to implement a generic query parsing and information API, allowing filters and routers to be used with different SQL variants. For now, however, module writers should not focus on protocol-independence.

## Writing a plugin

This section assumes that the *CPI* is available in the `include/maxscale/`-folder. Also,
it will refer to the *RoundRobinRouter* plugin. The source for the *RoundRobinRouter* is included in the module developer package and should provide a practical example on how to write a plugin for MariaDB MaxScale in C++.

### Basic definitions and headers

Generally, all type definitions, macros and functions exposed by the MaxScale core to be used by the modules are prefixed with `MXS`. This should avoid name collisions in the case a module ends up including many symbols from the CPI.

Every compilation unit should begin with `#define MXS_MODULE_NAME "<name>"`. This definition will be used by log macros for clarity, prepending `<name>` to every log message. Next, the module should `#include <maxscale/cppdefs.h>` (for C++) or `#include <maxscale/cdefs.h>` (for C). These headers contain compilation environment dependent definitions and global constants, and include some generally useful headers. Including one of them first in every source file enables later global redefinitions across all MaxScale modules. If your module is composed of multiple source files, the above should be placed to a common header file included in the beginning of the source files. The file with the module API definition should also include the header for the module type, e.g. `filter.h`.

Other common header files required by most modules are listed in the table below. All are located in the *include/maxscale* folder.

Header | Contents
-------|----------
`alloc.h` | Malloc, calloc etc. replacements
`buffer.h` | Packet buffer management
`config.h` | Configuration settings
`dcb.h` | I/O using descriptor control blocks
`debug.h` | Debugging macros
`modinfo.h` | Module information structure
`server.h` | Backend server information
`service.h` | Service definition
`session.h` | Client session definition
`logmanager.h` | Logging macros and functions

### Module information container

The module must implement the `MXS_CREATE_MODULE()`-function, which returns a pointer to a statically allocated `MXS_MODULE`-structure. This function is called by the module loader during program startup. `MXS_MODULE` (type defined in `modinfo.h`) contains function pointers to further module entrypoints, miscellaneous information about the module and the configuration parameters accepted by the module. This function must be exported without C++ name mangling, so in C++ code it should be defined `extern "C"`. Please see *RoundRobinRouter* source for a concrete example.

The information container describes the module in general and is constructed once during program excecution. A module may have multiple *instances* with different configuration parameters. For example, a filter module can be used with two different configurations in different services (or even in the same service). In this case the loader uses the same module information container for both but creates two module instances.

The MariaDB MaxScale configuration file `maxscale.cnf` is parsed by the core. The core also checks that all the defined parameters are of the correct type for the module in question. For this, the `MXS_MODULE`-structure includes a list of the parameters accepted by the module, defining parameter names, types and default values. In the actual module code, parameter values should be extracted using functions defined in `config.h`.

### Module API overview

This section explains some general concepts encountered when implementing a module API. Most of this section will apply to most module types. For more detailed information see the module specific section, header files or the doxygen documentation.

Modules with configuration data define an *INSTANCE* object, which is created by the module code in a `createInstance`-function or equivalent. The instance creation function is called during MaxScale startup, usually when creating services. The module instance data is held by the *SERVICE*-structure (or other higher level construct) and given as a parameter when calling functions from the module in question. The instance structure should contain all non-client-specific information required by the functions of the module. The core does not know what the object contains (since it is defined by the module itself), nor will it modify the pointer or the referenced object in any way. In the interface, the instance pointer is defined as `void*`, but in actual module code the pointer should always be cast to the actual instance type.

Modules dealing with client-specific data require a *SESSION* object for every client. As with the instance data, the definition of the module session structure is up to the writer and MaxScale treats it as an opaque type. Usually the session contains status indicators and any resources required by the client. MaxScale core has its own `MXS_SESSION` object, which tracks a variety of client related information. The `MXS_SESSION` is given as a parameter to module-specific session creation functions and is required for several typical operations such as connecting to backends.

Descriptor control blocks (`DCB`), are generalized I/O descriptor types. DCBs store the file descriptor, state, remote address, username, session, and other data. DCBs are created whenever a new socket is created. Typically this happens when a new client connects or MaxScale connects the client session to backend servers. The module writer should use DCB handling functions provided by the CPI to manage connections instead of calling general networking libraries.

Network data such as client queries and backend replies are held in a rather complicated buffer container type called `GWBUF`. Multiple GWBUFs can form a linked list with type information and properties in each GWBUF-node. Each node includes a pointer to a reference counted shared buffer (`SHARED_BUF`), which finally points to a slice of the actual data. What all this means is that multiple GWBUF-chains can share some parts of data while having some parts private. The construction is meant to minimize the need for data copying. In simple situations there is no need to form a linked list and the buffer acts similar to a handle. Plugin writers should use the CPI to manipulate GWBUFs.

In the next sections, the APIs of module types in MariaDB MaxScale are presented.

#### General module management

```java
int process_init()
void process_finish()
int thread_init()
void thread_finish()
```

These four functions are present in all `MXS_MODULE` structs and are not part of the API of any individual module type. `process_init` and `process_finish` are called by the module loader right after loading a module and just before unloading a module. Usually, these can be set to null in `MXS_MODULE` unless the module needs some general initializations before creating any instances. `thread_init` and `thread_finish` are thread specific equivalents.

```java
void diagnostics(INSTANCE *instance, DCB *dcb)
```

A diagnostics printing routine is present in nearly all module types, although with varying signatures. This entrypoint should print various statistics and status information about the module instance `instance` in string form. The target of the printing is the given DCB, and printing should be implemented by calling `dcb_printf`. The diagnostics function is used by the *MaxInfo* and *MaxAdmin* features.

#### Protocol

```java
int32_t (*read)(struct dcb *);
int32_t (*write)(struct dcb *, GWBUF *);
int32_t (*write_ready)(struct dcb *);
int32_t (*error)(struct dcb *);
int32_t (*hangup)(struct dcb *);
int32_t (*accept)(struct dcb *);
int32_t (*connect)(struct dcb *, struct server *, struct session *);
int32_t (*close)(struct dcb *);
int32_t (*listen)(struct dcb *, char *);
int32_t (*auth)(struct dcb *, struct server *, struct session *, GWBUF *);
int32_t (*session)(struct dcb *, void *);
char   *(*auth_default)();
int32_t (*connlimit)(struct dcb *, int limit);
```

Protocol modules are laborous to implement due to their low level nature. Each DCB maintains pointers to the correct protocol functions to be used with it, allowing the DCB to be used in a protocol-independent manner.

`read`, `write_ready`, `error` and `hangup` are *epoll* handlers for their respective events. `write` implements writing and is usually called in a router module. `accept` is a listener socker handler. `connect` is used during session creation when connecting to backend servers. `listen` creates a listener socket. `close` closes a DCB created by *accept*, *connect* or *listen*.

#### Authenticator

```java
void* (*initialize)(char **options);
void* (*create)(void* instance);
int   (*extract)(struct dcb *, GWBUF *);
bool  (*connectssl)(struct dcb *);
int   (*authenticate)(struct dcb *);
void  (*free)(struct dcb *);
void  (*destroy)(void *);
int   (*loadusers)(struct servlistener *);
void  (*diagnostic)(struct dcb*, struct servlistener *);
int (*reauthenticate)(struct dcb *, const char *user,
                      uint8_t *token, size_t token_len,
                      uint8_t *scramble, size_t scramble_len,
                      uint8_t *output, size_t output_len);
```

Authenticators must communicate with the client or the backends and implement authentication. The authenticators can be divided to client and backend modules, although the two types are linked and must be used together. Authenticators are also dependent on the protocol modules.

#### Filter and Router

Filter and router APIs are nearly identical and are presented together. Since these are the modules most likely to be implemented by outside developers they are discussed in more detail.

```java
INSTANCE* createInstance(SERVICE* service, char** options)
void destroyInstance(INSTANCE* instance)
```

`createInstance` should read the `options` and initialize an "instance" object for use with `service`. Often, simply saving the configuration values to fields is enough. `destroyInstance` is called when the service using the module is deallocated. It should free any resources claimed by the instance. All sessions created by this instance should be closed before calling the destructor.

```java
void* newSession(INSTANCE* instance, MXS_SESSION* mxs_session)
void closeSession(INSTANCE* instance, SESSION* session)
void freeSession(INSTANCE* instance, SESSION* session)
```

These functions manage sessions. `newSession` should allocate a router or filter session attached to the client session represented by `mxs_session`. MaxScale will pass the returned pointer to all the API entrypoints that process user data. `closeSession` should close connections the session has opened and release any resources specific to the served client. The *SESSION* structure allocated in `newSession` should not be deallocated by `closeSession` but in `freeSession`. Usually these two are called in succession by the core.

```java
int routeQuery(INSTANCE *instance, SESSION session, GWBUF* queue)
void clientReply(INSTANCE* instance, SESSION session, GWBUF* queue, DCB *backend_dcb);
```

These entrypoints are called for client requests which should be routed to backends, and for backend reply packets which should be routed to the client. For some modules, MaxScale is itself the backend.

`routeQuery` is often the most complicated function in a router, as it implements the routing logic. It typically considers the client request `queue`, the router settings in `instance` and the session state in `session` when making a routing decision. For filters, `routeQuery` also typically implements the main logic, although there the routing target is constant. For router modules, `routeQuery` should end in calling `dcb->func.write()`. Filters should directly call `routeQuery` for the next filter or router in the chain.

`clientReply` processes data flowing from backend back to client. For routers, this function is often much simpler than `routeQuey`, since there is only one client to route to. Depending on the router, some packets may not be routed to the client. For example, if a client query was routed to multiple backends, MaxScale will receive multiple replies while the client only expects one. Routers should pass the reply packet to last filter in the chain (reversed order) using the macro `MXS_SESSION_ROUTE_REPLY`. Filters should call the `clientReply` of the previous filter in the chain. There is no need for filters to worry about being the first filter in the chain, as this is handled transparently by the session creation routine.

```java
void handleError(INSTANCE* instance,SESSION* session, GWBUF* errmsgbuf,
                 DCB* problem_dcb, mxs_error_action_t action, bool* succp);
```

This router-only entrypoint is called if `routeQuery` returns an error value of if an error occurs in one of the connections listened to by the session. The steps an error handler typically takes depend on the nature of the `problem_dcb` and the error encountered. If `problem_dcb` is a client socket, then the session is lost and should be closed. The error handler should not do this by itself, and just report the failure by setting `succp` to false. If `problem_dcb` is a backend socket, then the error handler should try to connect to another backend if the routing logic allows. If the error is simply failed authentication on the backend, then it is usually best to send the message directly to the client.


```java
uint64_t getCapabilities(void)
```

This is a simple getter for router and filter capabilities. The return value is a bitfield resulting from ORring the individual capabilities. `routing.h` lists the allowed capability flags. The most common capability to set is `RCAP_TYPE_STMT_INPUT`, causing the protocol modules (if compatible) to only send complete statements to the filter/router chain.

#### Monitor

```java
void *(*startMonitor)(MXS_MONITOR *monitor, const MXS_CONFIG_PARAMETER *params);
void (*stopMonitor)(MXS_MONITOR *monitor);
void (*diagnostics)(DCB *, const MXS_MONITOR *);
```

Monitor modules typically run a repeated monitor routine with a used defined interval. The `MXS_MONITOR` is a standard monitor definition used for all monitors and contains a void pointer for storing module specific data. `startMonitor` should create a new thread for itself using functions in the CPI and have it regularly run a monitor loop. In the beginning of every monitor loop, the monitor should lock the `SERVER`-structures of its servers. This prevents any administrative action from interfering with the monitor during its pass.

## Core public interface

One could spend pages explaining the public headers in detail, what to do?

## Compiling and installing

The requirements for compiling a module are:
* The public headers (CPI)
* A compatible compiler, typically GCC (version X??)
* Libraries required by the public headers

The public header files themselves may include headers from other libraries. These libraries need to be installed and it may be required to point out their location to gcc. Some of the more commonly required libraries are:
 * *mySQL Connector-C* (mySQL or MariaDB version libmariadb-client-lgpl-dev), used by the mySQL protocol module
 * *pcre2 regular expressions* (libpcre2-dev), used for example by the header `modutil.h`

After all dependencies are accounted for, the module should compile with a command along the lines of
```
gcc -Iinclude/ -I/usr/include/mariadb -shared -fPIC -g -o libmymodule.so mymodule.cpp
```
For large modules one may need to set up more complicated compilation, but that is not in the scope of this document.


## Running, testing and profiling?

Is this section needed?
