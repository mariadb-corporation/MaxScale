# MariaDB MaxScale 2.2.0 Release Notes

Release 2.2.0 is a Beta release.

This document describes the changes in release 2.2.0, when compared to
release 2.1.X.

For any problems you encounter, please consider submitting a bug
report at [Jira](https://jira.mariadb.org).

## Changed Features

### NamedServerFilter

This filter now uses the PCRE2-libarary to match queries. Previously, it used
the POSIX-version of PCRE2. The filter also accepts multiple match-server pairs.
Please see the NamedServerFilter documentation for details.

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

MaxAdmin no longer attempts to interpret additional command line parameters as a
file name to load commands from  (e.g. `maxadmin mycommands.txt`). The shell
indirection operator `<` should be used to achieve the same effect (`maxadmin <
mycommands.txt`).

## New Features

### MySQL Monitor Crash Safety

The MySQL monitor keeps a journal of the state of the servers and the currently
elected master. This information will be read if MaxScale suffers an
uncontrolled shutdown. By doing the journaling of server states, the mysqlmon
monitor is able to keep track of stale master and stale slave states across
restarts and crashes.

### Avrorouter `deflate` compression

The Avrorouter now supports the `deflate` compression method. This allows the
stored Avro format files to be compressed on disk. For more information, refer
to the [Avrorouter](../Routers/Avrorouter.md) documentation.

### Preliminary proxy protocol support

The MySQL backend protocol module now supports sending a proxy protocol header
to the server. For more information, see the server section in the
[Configuration guide](../Getting-Started/Configuration-Guide.md).

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
