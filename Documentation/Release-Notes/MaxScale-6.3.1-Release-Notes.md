# MariaDB MaxScale 6.3.1 Release Notes

Release 6.3.1 is a GA release.

This document describes the changes in release 6.3.1, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-6.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## New Features

* [MXS-2904](https://jira.mariadb.org/browse/MXS-2904) Document MaxScale performance tuning

## Bug fixes

* [MXS-4132](https://jira.mariadb.org/browse/MXS-4132) router_options=master ignores rank for first server
* [MXS-4121](https://jira.mariadb.org/browse/MXS-4121) MaxCtrl is limited to 2GB of memory
* [MXS-4120](https://jira.mariadb.org/browse/MXS-4120) Avrorouter crash with a SEQUENCE engine table
* [MXS-4113](https://jira.mariadb.org/browse/MXS-4113) namedserverfilter does not work with targets parameter
* [MXS-4112](https://jira.mariadb.org/browse/MXS-4112) python mariab can cause maxsccale to crash
* [MXS-4111](https://jira.mariadb.org/browse/MXS-4111) Extra warnings logged even with use_sql_variables_in=master
* [MXS-4110](https://jira.mariadb.org/browse/MXS-4110) Schemarouter does not ignore the sys schema
* [MXS-4109](https://jira.mariadb.org/browse/MXS-4109) The /user/inet endpoint fails schema validation
* [MXS-4101](https://jira.mariadb.org/browse/MXS-4101) Unexpected result with mixed 10.2 and 10.6 backends
* [MXS-4099](https://jira.mariadb.org/browse/MXS-4099) Crash with match/exclude in kafkacdc
* [MXS-4096](https://jira.mariadb.org/browse/MXS-4096) Binlog Routers SHOW SLAVE STATUS does not show SSL information
* [MXS-4095](https://jira.mariadb.org/browse/MXS-4095) Maxscale CDC to Kafka job is failing for few tables
* [MXS-4092](https://jira.mariadb.org/browse/MXS-4092) schemarouter: duplicate tables found, if table differs on  upper lower case only
* [MXS-4091](https://jira.mariadb.org/browse/MXS-4091) Maxscale Redis | Spyder Deployments are failing (OperationalError: (2013, 'Lost connection to MySQL server during query'))
* [MXS-4088](https://jira.mariadb.org/browse/MXS-4088) The parameter's tooltip shows unnecessary attributes
* [MXS-4086](https://jira.mariadb.org/browse/MXS-4086) REST API allows deletion of last user
* [MXS-4084](https://jira.mariadb.org/browse/MXS-4084) Client port is not in 'maxctrl show sessions' output
* [MXS-4059](https://jira.mariadb.org/browse/MXS-4059) Make query editor visualization feature easier to use

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
