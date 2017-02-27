# MariaDB MaxScale 2.2.0 Release Notes

Release 2.2.0 is a Beta release.

This document describes the changes in release 2.2.0, when compared to
release 2.1.X.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### Blah

## Dropped Features

### MaxAdmin

The following deprecated commands have been removed:

* `enable log [debug|trace|message]`
* `disable log [debug|trace|message]`
* `enable sessionlog [debug|trace|message]`
* `disable sessionlog [debug|trace|message]`

The following commands have been deprecated:

* `enable sessionlog-priority <session-id> [debug|info|notice|warning]`
* `disable sessionlog-priority <session-id> [debug|info|notice|warning]`

The commands can be issued, but have no effect.

## New Features

### Avrorouter `deflate` compression

The Avrorouter now supports the `deflate` compression method. This allows the
stored Avro format files to be compressed on disk. For more information, refer
to the [Avrorouter](../Routers/Avrorouter.md) documentation.

## Bug fixes

[Here is a list of bugs fixed since the release of MaxScale 2.1.X.]()

## Known Issues and Limitations

There are some limitations and known issues within this version of MaxScale.
For more information, please refer to the [Limitations](../About/Limitations.md) document.

## Packaging

RPM and Debian packages are provided for the Linux distributions supported
by MariaDB Enterprise.

Packages can be downloaded [here](https://mariadb.com/resources/downloads).

## Source Code

The source code of MaxScale is tagged at GitHub with a tag, which is identical
with the version of MaxScale. For instance, the tag of version X.Y.Z of MaxScale
is X.Y.Z. Further, *master* always refers to the latest released non-beta version.

The source code is available [here](https://github.com/mariadb-corporation/MaxScale).
