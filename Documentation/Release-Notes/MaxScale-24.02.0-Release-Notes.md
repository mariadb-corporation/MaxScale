# MariaDB MaxScale 24.02.0 Release Notes -- 2024-02-

Release 24.02.0 is a Beta release.

This document describes the changes in release 24.02, when compared to
release 23.08.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Changed Features

### [MXS-4705](https://jira.mariadb.org/browse/MXS-4705) Support multiple IPs for one server

### [MXS-4746](https://jira.mariadb.org/browse/MXS-4746) session_track_system_variables set up including last_gtid when enable causal_read

### [MXS-4748](https://jira.mariadb.org/browse/MXS-4748) Add functionality so that rebuild-server will work with non default configuration of datadir and binlogs

## Dropped Features

###

## Deprecated Features

## New Features

### [MXS-3616](https://jira.mariadb.org/browse/MXS-3616) Support MARIADB_CLIENT_EXTENDED_TYPE_INFO

### [MXS-3986](https://jira.mariadb.org/browse/MXS-3986) Binlog compression and archiving

### [MXS-4191](https://jira.mariadb.org/browse/MXS-4191) Restrict the REST API user's authentication to specific IP's only like MariaDB

### [MXS-4764](https://jira.mariadb.org/browse/MXS-4764) KafkaCDC: Option to use the latest GTID

### [MXS-4774](https://jira.mariadb.org/browse/MXS-4774) Add support for ephemeral server certificates

### MaxGUI
Numerous additions have been added and improvements made to MaxGUI.
The most notable ones are listed here:

### [MXS-3620](https://jira.mariadb.org/browse/MXS-3620) Filter log data by module, object, session id on the GUI

### [MXS-3851](https://jira.mariadb.org/browse/MXS-3851) Show more KafkaCDC router statistics on GUI

### [MXS-3919](https://jira.mariadb.org/browse/MXS-3919) Add `interactive_timeout` and `wait_timeout` inputs in the Query Editor settings

### [MXS-4017](https://jira.mariadb.org/browse/MXS-4017) Query Editor Auto completion for all identifier names of the active schema

### [MXS-4143](https://jira.mariadb.org/browse/MXS-4143) Able to export columns data with table structure for only those selected columns like sqlYog

### [MXS-4375](https://jira.mariadb.org/browse/MXS-4375) Logs Archive Filter Between Dates

### [MXS-4466](https://jira.mariadb.org/browse/MXS-4466) Greater detail / customization of Maxscale GUI Dashboard Load

### [MXS-4447](https://jira.mariadb.org/browse/MXS-4447) Show column definition in the Query Editor

### [MXS-4535](https://jira.mariadb.org/browse/MXS-4535) Connection resource type user preference

### [MXS-4572](https://jira.mariadb.org/browse/MXS-4572) Improve UX Setting up with GUI

### [MXS-4770](https://jira.mariadb.org/browse/MXS-4770) Show config sync information in the GUI

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
