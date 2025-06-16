# MariaDB MaxScale 25.01.3 Release Notes

Release 25.01.3 is a GA release.

NOTE: The MaxScale package has been renamed from maxscale-enterprise back
      to maxscale, which may, when repositories are used, require 25.01.1 and
      25.01.2 to be removed before installing 25.01.3.

This document describes the changes in release 25.01.3, when compared to the
previous release in the same series.

If you are upgrading from an older major version of MaxScale, please read the
[upgrading document](../Upgrading/Upgrading-To-MaxScale-25.01.md) for
this MaxScale version.

For any problems you encounter, please consider submitting a bug
report on [our Jira](https://jira.mariadb.org/projects/MXS).

## Bug fixes

* [MXS-5626](https://jira.mariadb.org/browse/MXS-5626) Network options for maxvisualize are not documented
* [MXS-5625](https://jira.mariadb.org/browse/MXS-5625) maxpostprocess install failure leaves a corrupt installation
* [MXS-5618](https://jira.mariadb.org/browse/MXS-5618) Maxctrl interactive mode doesn't use --tls-verify-server-cert=false
* [MXS-5613](https://jira.mariadb.org/browse/MXS-5613) The logout screen is shown when accessing the MaxGUI login view.
* [MXS-5599](https://jira.mariadb.org/browse/MXS-5599) Processing of conditional headers is incorrect
* [MXS-5598](https://jira.mariadb.org/browse/MXS-5598) MaxCtrl fails to read large inputs from stdin
* [MXS-5597](https://jira.mariadb.org/browse/MXS-5597) admin_oidc_url is documented to not be dynamic when in fact it is
* [MXS-5590](https://jira.mariadb.org/browse/MXS-5590) REST-API always sends a Connection: close header
* [MXS-5588](https://jira.mariadb.org/browse/MXS-5588) Signal 11 crash when enabling causal reads with Galera
* [MXS-5584](https://jira.mariadb.org/browse/MXS-5584) Diff-router does not decrypt password before stopping replication
* [MXS-5583](https://jira.mariadb.org/browse/MXS-5583) Diff-router should log more information when starting
* [MXS-5582](https://jira.mariadb.org/browse/MXS-5582) Add a Service with a CLUSTER as its target breaks CONFIG SYNC
* [MXS-5581](https://jira.mariadb.org/browse/MXS-5581) Diff-router's behaviour is confusing when it fails to start
* [MXS-5577](https://jira.mariadb.org/browse/MXS-5577) Aborted connection on backend mariadb with persistpool maxscale
* [MXS-5576](https://jira.mariadb.org/browse/MXS-5576) Maxctrl config permission check error message is misleading
* [MXS-5567](https://jira.mariadb.org/browse/MXS-5567) Wrong password in interactive mode is only seen after the first command
* [MXS-5566](https://jira.mariadb.org/browse/MXS-5566) --secretsdir has no default value
* [MXS-5564](https://jira.mariadb.org/browse/MXS-5564) Query Editor default connection type preference is changed unexpectedly
* [MXS-5563](https://jira.mariadb.org/browse/MXS-5563) Using PKCS#1 private key in the REST-API results in cryptic errors
* [MXS-5556](https://jira.mariadb.org/browse/MXS-5556) Trailing parts of large session command are not routed correctly
* [MXS-5544](https://jira.mariadb.org/browse/MXS-5544) Prepared statements fail through schemarouter for Columnstore
* [MXS-5525](https://jira.mariadb.org/browse/MXS-5525) Masking with functions uses wrong rule settings
* [MXS-5314](https://jira.mariadb.org/browse/MXS-5314) Resultset table not fully expanded for inactive query tab

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the supported Linux distributions.

Packages can be downloaded [here](https://mariadb.com/downloads/#mariadb_platform-mariadb_maxscale).
