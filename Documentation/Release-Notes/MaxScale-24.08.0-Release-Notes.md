# MariaDB MaxScale 24.08.0 Release Notes --

Release 24.08.0 is a Beta release.

This document describes the changes in release 24.08, when compared to
release 24.02.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### [MXS-5122](https://jira.mariadb.org/browse/MXS-5122) Scale everything according to the amount of available container resources

If MaxScale is running in a container, it will adapt to amount of resources (CPUs and
memory) available in the container. See [threads](../Getting-Started/Configuration-Guide.md#threads)
and [query_classifier_cache_size](../Getting-Started/Configuration-Guide.md#query_classifier_cache_size)
for more information.

## Dropped Features

## Deprecated Features

## New Features

### [MXS-3628](https://jira.mariadb.org/browse/MXS-3628) Fully support inclusion and exclustion in projection document of find
### [MXS-4842](https://jira.mariadb.org/browse/MXS-4842) Safe failover and safe auto_failover
### [MXS-4897](https://jira.mariadb.org/browse/MXS-4897) Add admin_ssl_cipher setting
### [MXS-4986](https://jira.mariadb.org/browse/MXS-4986) Add low overhead trace logging
### [MXS-5016](https://jira.mariadb.org/browse/MXS-5016) Add support for MongoDB Compass
### [MXS-5037](https://jira.mariadb.org/browse/MXS-5037) Track reads and writes at the server level
### [MXS-5041](https://jira.mariadb.org/browse/MXS-5041) Don't replay autocommit statements with transaction_replay
### [MXS-5047](https://jira.mariadb.org/browse/MXS-5047) Test primary server writablity in MariaDB Monitor
### [MXS-5049](https://jira.mariadb.org/browse/MXS-5049) Implement host_cache_size in MaxScale
### [MXS-5069](https://jira.mariadb.org/browse/MXS-5069) support bulk returning all individual results
### [MXS-5075](https://jira.mariadb.org/browse/MXS-5075) Add switchover option which leaves old primary server to maintenance mode
### [MXS-5136](https://jira.mariadb.org/browse/MXS-5136) Extend the number of supported aggregation stages and operations.

### MaxGUI

Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

* [MXS-3852](https://jira.mariadb.org/browse/MXS-3852) Show Only SQL editor Sessions/Connections Status Separately On Maxscale GUI
* [MXS-3952](https://jira.mariadb.org/browse/MXS-3952) Auto-inject `LIMIT` and `OFFSET`, and allow no limit
* [MXS-4370](https://jira.mariadb.org/browse/MXS-4370) Auto expand active schema node
* [MXS-4849](https://jira.mariadb.org/browse/MXS-4849) Export the visualized configuration graph
* [MXS-4886](https://jira.mariadb.org/browse/MXS-4886) Query Editor: Add UI for creating and altering object

## Bug fixes

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is `maxscale-X.Y.Z`. Further, the default branch is always the latest GA version
of MaxScale.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
