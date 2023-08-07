# MariaDB MaxScale MaxGUI Guide

[TOC]

# Introduction

_MaxGUI_ is a browser-based interface for MaxScale REST-API and query execution.

# Enabling MaxGUI

To enable MaxGUI in a testing mode, add `admin_host=0.0.0.0` and
`admin_secure_gui=false` under the `[maxscale]` section of the MaxScale
configuration file. Once enabled, MaxGUI will be available on port 8989:
`http://127.0.0.1:8989/`

## Securing the GUI

To make MaxGUI secure, set `admin_secure_gui=true` and configure both the
`admin_ssl_key` and `admin_ssl_cert` parameters.

See [Configuration Guide](./Configuration-Guide.md) and
[Configuration and Hardening](../Tutorials/REST-API-Tutorial.md#configuration-and-hardening)
for instructions on how to harden your MaxScale installation for production use.

# Authentication

MaxGUI uses the same credentials as `maxctrl`. The default username is `admin`
with `mariadb` as the password.

Internally, MaxGUI uses [JSON Web Tokens](https://jwt.io/introduction/) as the
authentication method for persisting the user's session. If the _Remember me_
checkbox is ticked, the session will persist for 24 hours. Otherwise, the
session will expire as soon as MaxGUI is closed.

To log out, simply click the username section in the top right corner of the
page header to access the logout menu.

# Pages

## Dashboard

This page provides an overview of MaxScale configuration which includes
Monitors, Servers, Services, Sessions, Listeners, and Filters.

By default, the refresh interval is 10 seconds.

## Detail

This page shows information on each
[MaxScale object](./Configuration-Guide.md#objects) and allow to edit its
parameter, relationships and perform other manipulation operations.

Access this page by clicking on the MaxScale object name on the
[dashboard page](#dashboard)

## Visualization

This page visualizes MaxScale configuration and clusters.

- Configuration: Visualizing MaxScale configuration.
- Cluster: Visualizing a replication cluster into a tree graph and provides
  manual cluster manipulation operations such as
  `switchover, reset-replication, release-locks, failover, rejoin` . At the
  moment, it supports only servers monitored by Monitor using
  [mariadbmon](../Monitors/MariaDB-Monitor.md) module.

Access this page by clicking the graph icon on the sidebar navigation.

## Settings

This page shows and allows editing of MaxScale parameters.

Access this page by clicking the gear icon on the sidebar navigation.

## Logs Archive

Realtime MaxScale logs can be accessed by clicking the logs icon on the sidebar
navigation.

## Workspace

The "Workspace" page offers a versatile set of tools for effectively managing
data and database interactions. It includes the following key tasks:

### 1. Run Queries

Execute queries on various servers, services, or listeners to retrieve data and
perform database operations. Visualize query results using different graph
types such as line, bar, or scatter graphs. Export query results in formats
like CSV or JSON for further analysis and sharing.

### 2. Data Migration

The "Data Migration" feature facilitates seamless transitions from PostgreSQL
to MariaDB. Transfer data and database structures between the two systems while
ensuring data integrity and consistency throughout the process.

### 3. Create an ERD

Generating Entity-Relationship Diagrams (ERDs) to gain insights regarding data
structure, optimizing database design for both efficiency and clarity.
