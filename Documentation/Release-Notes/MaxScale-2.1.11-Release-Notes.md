# MariaDB MaxScale 2.1.11 Release Notes -- 2017-11-21

Release 2.1.11 is a GA release.

This document describes the changes in release 2.1.11, when compared
to release [2.1.10](MaxScale-2.1.10-Release-Notes.md).

If you are upgrading from release 2.0, please also read the following
release notes:

* [2.1.10](./MaxScale-2.1.10-Release-Notes.md)
* [2.1.9](./MaxScale-2.1.9-Release-Notes.md)
* [2.1.8](./MaxScale-2.1.8-Release-Notes.md)
* [2.1.7](./MaxScale-2.1.7-Release-Notes.md)
* [2.1.6](./MaxScale-2.1.6-Release-Notes.md)
* [2.1.5](./MaxScale-2.1.5-Release-Notes.md)
* [2.1.4](./MaxScale-2.1.4-Release-Notes.md)
* [2.1.3](./MaxScale-2.1.3-Release-Notes.md)
* [2.1.2](./MaxScale-2.1.2-Release-Notes.md)
* [2.1.1](./MaxScale-2.1.1-Release-Notes.md)
* [2.1.0](./MaxScale-2.1.0-Release-Notes.md)

For any problems you encounter, please consider submitting a bug report at
[Jira](https://jira.mariadb.org).

## Changed Features

### Peer Certificate Verification

The SSL peer certificate verification can now be disabled for servers and
listeners by adding `ssl_verify_peer_certificate=false` to the respective
definitions.

## Bug fixes

[Here is a list of bugs fixed in MaxScale 2.1.11.](https://jira.mariadb.org/issues/?jql=project%20%3D%20MXS%20AND%20issuetype%20%3D%20Bug%20AND%20status%20%3D%20Closed%20AND%20fixVersion%20%3D%202.1.11)

* [MXS-1518](https://jira.mariadb.org/browse/MXS-1518) Wrong parameter name for ssl_cert_verify_depth
* [MXS-1500](https://jira.mariadb.org/browse/MXS-1500) Invalid characters in real_type schema field

## Packaging

RPM and Debian packages are provided for the Linux distributions supported by
MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is maxscale-X.Y.Z.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
