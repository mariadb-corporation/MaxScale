# MariaDB MaxScale MaxGUI Guide

[TOC]

# Introduction

MaxGUI is the new browser based tool for configuring and managing
MaxScale. It is a more user friendly and intuitive to use companion
to the command line tool `maxctrl`.

# Enabling MaxGUI

In order to make MaxGUI works, the following parameters al least needs to be enabled in MaxScale configuration file:
`admin_enabled`, `admin_gui`, `admin_ssl_key`, `admin_ssl_cert`. Check [Configuration-Guide](./Configuration-Guide.md) for information.

The root REST API resource (i.e. `http://localhost:8989/`) will
serve MaxGUI

# Features

## Authentication

To access the dashboard, credentials need to be provided. MaxGUI uses the same credentials as `maxctrl`.
By default , username is `admin` and password is `mariadb`.

MaxGUI using JWT as an authentication method to persist user's session. By default, if "Remember me" checkbox is ticked, it will persist user's session for 8 hours, otherwise, as soon as user close the tab from the browser, user's session is expired.

To log out, simply clicking username section in the top right corner of page header to show logout menu.

## Dashboard page

Dashboard shows three graphs:

Sessions graph illustrates the total number of current sessions updating every 10 seconds.
Connections graph shows servers's current connections updating every 10 seconds.
Load graph show the last second load of thread which is updated every second.

Under the graphs section, there is a tab navigation allowing to switch table view which contains overview information of the following resources: all servers grouped by monitor, current sessions and all services.
The information of these resources are updated every 10 seconds.

## Detail page

Resources monitors, server and services have their own details page. It can be accessed by clicking resource name on the dashboard page.

In details page, resource parameters as well as relationships can be modified in its parameters section.
Deletion of a resource or other settings can be done by clicking the setting icon located next to the resource name. Eg: Clicking the setting icon in Monitor detail page will popup three icons allowing to stop, start and delete monitor, respectively.

For Server, monitor relationship can be modified by hovering rectangular "Monitor" block section at the top of the page.

## Resource creation

Creation of resource can be done by clicking the "+ Create New" button at the top right corner of dashboard page or detail page. Notice: Resources using modules can be created only if the module of that resource is loaded.

## Viewing and modifying Maxscale parameters

MaxScsale parameters can be viewed and modified at runtime on the Settings page which can be accessed by clicking the settings icon in the sidebar navigation.

## Global search

The global search input located next to the "+ Create New" button allows to search for keywords in tables. Eg: type undefined in dashboard page, tab servers will show records with undefined values.

# Limitations

For all resource creation such as filters, services, monitors and listeners, only loaded modules will be available. Eg: If there is no filter modules is loaded when run MaxScale, MaxGui can not get filter modules, therefore, filter resource can no be created.
