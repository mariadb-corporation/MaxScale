# MariaDB MaxScale MaxGUI Guide

[TOC]

# Introduction

_MaxGUI_ is the new browser based tool for configuring and managing
MaxScale. It is a more user friendly and intuitive to use companion
to the command line tool `maxctrl`.

# Enabling MaxGUI

To enable MaxGUI in a testing mode, add `admin_host=0.0.0.0` and
`admin_secure_gui=false` under the `[maxscale]` section of the MaxScale
configuration file. Once enabled, MaxGUI will be available on port 8989:
`http://127.0.0.1:8989/`

## Securing the GUI

To make MaxGUI secure, set `admin_secure_gui=true` and configure both the
`admin_ssl_key` and `admin_ssl_cert` parameters.

Check the [Configuration Guide](./Configuration-Guide.md) for the parameter
documentation and read the _Configuration and Hardening_ section of the
[REST API tutorial](../Tutorials/REST-API-Tutorial.md) for instructions on
how to harden your MaxScale installation for production use.

# Authentication

MaxGUI cannot be used without providing credentials. MaxGUI uses
the same credentials as `maxctrl`, so by default, the username is
`admin` and the password is `mariadb`.

MaxGUI uses [JSON Web Tokens](https://jwt.io/introduction/) as the
authentication method for persisting the user's session.
If the _Remember me_ checkbox is ticked, the session will persist for
8 hours, otherwise, as soon as MaxGUI is closed, the session will expire.

To log out, simply click the username section in the top right corner of
the page header to access the logout menu.

# Features

## Dashboard page

The dashboard shows three graphs:

-   _Sessions_ graph illustrates the total number of current sessions
    updating every 10 seconds.

-   _Connections_ graph shows servers current connections
    updating every 10 seconds.

-   _Load_ graph shows the last second load of thread,
    which is updated every second.

Under the graphs section, there is a tab navigation allowing to switch
table view which contains overview information of the
following resources: all servers grouped by monitor, current sessions
and all services. The information of these resources are
updated every 10 seconds.

## Detail page

The monitor, server and services resources have their own details page.
It can be accessed by clicking the resource name on the dashboard page.

In the details page, resource parameters as well as relationships
can be modified in the resource's parameters section.

Deletion of a resource or other actions can be done by clicking the
setting icon located next to the resource name.
For instance, clicking the setting icon in Monitor detail page will
popup three icons allowing a monitor to be started, stopped, and deleted.

For Servers, the monitor relationship can be modified by hovering
over the rectangular _Monitor_ block section at the top of the page.

## SQL editor page
The query editor page can be accessed via the sidebar navigation menu.
If no active connection exists, a dialog to setup one is displayed.
A connection can target a server, service or listener.

The query layout editor interface comprises the following sections:
### The taskbar
Located at the top of the page, the taskbar contains resource quick action
buttons, query action buttons to run and manipulate the result.
* Resource quick action buttons are used to configure current active
connection and active database.
* Query action buttons are used to run SQL statements, visualize
query result in graphs and configure query settings.
* In addition, on the right corner of the taskbar, there is a
maximize/minimize button to set the page to full screen mode.

###  The schema sidebar
List databases, tables and columns of the server.
The list is fetched when needed and provided with
dedicated options which can be accessed by clicking the more
options (...) icon.
For example,  for table, it has options to _Preview Data_
and _View Details_ which internally executes `SELECT * FROM database.table_name`
and `DESCRIBE database.table_name`, respectively.

### The sql editor
* Writing SQL statements and executing all statements
or selected statements by using shortcut keys. In general, the editor
provides some of the most famous editor features of Visual Studio Code
* All the command palette can be shown by pressing F1 or right click
on the editor section to open context menu and choose _Command Palette_

### The query result
Located at the bottom of the page, the result section is comprised of two
tabs which are _Results_ and _Data Preview_.
* The _Results_ tab shows the query results of the SQL statements from
the sql editor.
* The _Data Preview_ contains two sub-tabs _DATA_ and _DETAILS_ showing
the results of _Preview Data_ and _View Details_, respectively.

Result tables can be filtered, sorted (only when total rows <= 10000)
and exported. The result can be exported as `json`, `csv` with custom delimiter.

The table result can be converted to vertical mode by clicking the _transform_ icon
on the right corner above the table. In addition, table columns visibility can be
toggled and resized.

### Query result visualization
The query result can be visualized into line, scatter, vertical bar and horizontal
bar graphs. In order to see the graph, simply clicking the _graph_ icon next to
the _gear_ icon in the task bar, then fill in the inputs.

The graph can be resized by clicking the border of the pane and exported as `jpeg`
format with same ratio.

### Current limitations
Only one connection can be created at a time and there is no option to save the
connection. Once user leaves the page, the connection will be disconnected.
The hard limit rows of each result is set to 10000 rows. Though it can be increased,
it can cause MaxScale to run out of memory. In addition, pagination of query result
set is not supported. To workaround this, use LIMIT offset, row_count to navigate
through the result set.

## Resource creation

A resource can be created by clicking the _+ Create New_ button at
the top right corner of the dashboard or the detail page.

*Note*: Resources that depend on a module can be created only if that
module has been loaded by MaxScale.

## Viewing and modifying Maxscale parameters

MaxScale parameters can be viewed and modified at runtime on the Settings
page, which can be accessed by clicking the settings icon in the sidebar
navigation.

## Global search

The global search input located next to the _+ Create New_ button can be
used for searching for keywords in tables.

