# About MaxScale
The MariaDB Corporation **MaxScale** is an intelligent proxy that allows
the forwarding of database statements to one or more database servers.
The forwarding is performed using rules that can be based on a the semantic
understanding of the database statements and on the roles of the various
servers within the backend cluster of databases.

MaxScale is designed to provide, transparently to applications, load
balancing and high availability functionality. In addition, it provides
a highly scalable and flexible architecture, with plugin components to
support different protocols and routing approaches.

MaxScale makes extensive use of the asynchronous I/O capabilities of the
Linux operating system, combined with a fixed number of worker threads.
The epoll system is used to provide the event driven framework for the
input and output via sockets. Similar features in Windows&reg; could
be used in future development of MaxScale.

Many of the services provided by MaxScale are implemented as external
shared object modules, which are loaded at runtime. These modules
support a fixed interface, communicating the entry points via a structure
consisting of a set of function pointers. This structure is called the
"module object". Additional modules can be created to work with MaxScale.

One group of modules provides support for protocols, both for clients
that communicate with MaxScale and for backend servers. The code that
routes the queries to the backend servers is also loaded as external
shared objects and they are referred to as routing modules. Another
group of modules work on data as it passes through MaxScale, and they
are known as filters.

A Google Group exists for MaxScale that can be used to discuss ideas,
issues and communicate with the MaxScale community:
Send email to [maxscale@googlegroups.com](mailto:maxscale@googlegroups.com)
	or use the [forum](http://groups.google.com/forum/#!forum/maxscale) interface

Bugs can be reported in the MariaDB Jira
	[https://mariadb.atlassian.net](https://mariadb.atlassian.net)

## Installing MaxScale
Information about installing MaxScale, either from a repository or by
building from source code, is included in the
[MariaDB MaxScale Installation Guide](../Getting-Started/MariaDB-MaxScale-Installation-Guide.md).

The same guide also provides basic information on running MaxScale.
More detailed information about configuring MaxScale is given in the
[Configuration Guide](../Getting-Started/Configuration-Guide.md).
