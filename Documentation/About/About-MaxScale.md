# About MaxScale
The MariaDB Corporation **MaxScale** is an intelligent proxy that allows forwarding of database statements to one or more database servers using complex rules, which can be based on a semantic understanding of the database statements and the roles of the various servers within the backend cluster of databases.

MaxScale is designed to provide load balancing and high availability functionality transparently to the applications. In addition it provides a highly scalable and flexibile architecture, with plugin components to support different protocols and routing decisions.

MaxScale is implemented in C so as to operate speedily. It also makes extensive use of the asynchronous I/O capabilities of the Linux operating system. The epoll system is used to provide the event driven framework for the input and output via sockets. Similar features in Windows&reg; could be used in future development of MaxScale.

Many of the services provided by MaxScale are implemented as external shared object modules which can be loaded at runtime. These modules support a fixed interface, communicating the entry points via a structure consisting of a set of function pointers. This structure is called the "module object". Additional modules can be created to work with MaxScale.

One group of modules provides support for protocols, both for clients that communicate with MaxScale and for backend servers. The code that routes the queries to the backend servers is also loaded as external shared objects and they are referred to as routing modules. Another group of modules work on data as it passes through MaxScale, and they are known as filters.

A Google Group exists for MaxScale that can be used to discuss ideas, issues and communicate with the MaxScale community:
Send email to [maxscale@googlegroups.com](mailto:maxscale@googlegroups.com)
	or use the [forum](http://groups.google.com/forum/#!forum/maxscale) interface
	
Bugs can be reported in the MariaDB Corporation bugs database
	[bug.mariadb.com](http://bugs.mariadb.com)

## Installing MaxScale
Information about installing MaxScale, either from a repository or by building from source code, is included in the guide [Getting Started with MaxScale](/Documentation/Getting-Started/Getting-Started-With-MaxScale.md).

The same guide also provides basic information on running MaxScale. More detailed information about configuring MaxScale is given in the [Configuration Guide](/Documentation/Getting-Started/Configuration-Guide.md).

