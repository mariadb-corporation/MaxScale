# MariaDB MaxScale MaxGUI Guide

[TOC]

# Introduction

_MaxGUI_ is the new browser based tool for configuring and managing
MaxScale. It is a more user friendly and intuitive to use companion
to the command line tool `maxctrl`.

# Enabling MaxGUI

In order to enable MaxGUI, at least the following parameters must be
set in the MaxScale configuration file: `admin_ssl_key`, `admin_ssl_cert`.
Check [Configuration-Guide](./Configuration-Guide.md) for details.

To prevent the use of MaxGUI, set `admin_gui` to `false` in the
configuration file.

Once enabled, MaxGUI can be accessed from `https://127.0.0.1:8989/`.

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

# Limitations

Resources that depend upon a module - i.e. filters, services, monitors and
listeners -  can only be created using a module that already has been
loaded by MaxScale. For instance, if no filter module has been loaded by
MaxScale, then it will not be possible to create a filter via MaxGUI.
This limitation will be removed before the GA release.
