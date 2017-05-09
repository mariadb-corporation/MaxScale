# MaxScale by MariaDB Corporation

[![Build Status](https://travis-ci.org/mariadb-corporation/MaxScale.svg?branch=develop)](https://travis-ci.org/mariadb-corporation/MaxScale)

The MariaDB Corporation MaxScale is an intelligent proxy that allows
forwarding of database statements to one or more database servers using
complex rules, a semantic understanding of the database statements and the
roles of the various servers within the backend cluster of databases.

MaxScale is designed to provide load balancing and high availability
functionality transparently to the applications. In addition it provides
a highly scalable and flexible architecture, with plugin components to
support different protocols and routing decisions.

MaxScale is implemented in C and makes extensive use of the
asynchronous I/O capabilities of the Linux operating system. The epoll
system is used to provide the event driven framework for the input and
output via sockets.

The protocols are implemented as external shared object modules which
can be loaded at runtime. These modules support a fixed interface,
communicating the entries points via a structure consisting of a set of
function pointers. This structure is called the "module object".

The code that routes the queries to the database servers is also loaded
as external shared objects and are referred to as routing modules.

An Google Group exists for MaxScale that can be used to discuss ideas,
issues and communicate with the MaxScale community.

- Email: maxscale@googlegroups.com
- Forum: http://groups.google.com/forum/#!forum/maxscale

We're also on the #maria and #maxscale channels on FreeNode.

Please report all feature requests, improvements and bugs in the [MariaDB Jira](https://jira.mariadb.org/projects/MXS/issues).

# Documentation

For information about installing and using MaxScale, please refer to the
documentation. The official documentation can be found on the
[MariaDB Knowledge Base](https://mariadb.com/kb/en/mariadb-enterprise/maxscale/).

- [MariaDB MaxScale 2.1 Documentation](https://mariadb.com/kb/en/mariadb-enterprise/6308/)
- [MariaDB MaxScale 2.0 Documentation](https://mariadb.com/kb/en/mariadb-enterprise/mariadb-maxscale-20-contents/)
- [MariaDB MaxScale 1.4 Documentation](https://mariadb.com/kb/en/mariadb-enterprise/mariadb-maxscale-14/maxscale-maxscale-contents/)

The module and configuration documentation can be found in the _Documentation_
directory of the source tree.

# Contributing Code

Read the [Contributing](https://github.com/mariadb-corporation/MaxScale/wiki/Contributing)
page on the wiki for more information on how to do pull request and where to do
them.
