# Workload Capture and Replay

[TOC]

## Overview

The _WCAR_ filter (module `wcar`) captures client traffic and stores it in a replayable format.

WCAR is designed to capture traffic on a production MaxScale instance. The captured data can
then be used as a reproducible way of generating client traffic without having to write
application-specific traffic generators.

The captured workloads can be used:
* to verify that upgrades of MariaDB behave as expected.
* to repeatedly measure effects of configuration changes, which is useful for database tuning.
* investigate why certain scenarios take longer then expected, as a kind of SQL debugging tool.

## Prerequisites

* Both the capture MaxScale and replay MaxScale servers must use the same
  linux distribution and CPU architecture. For example, if the capture was taken
  on an x86_64 RHEL 8 instance, the replay should also happen on an x86_64 RHEL
  8 instance. Captured workloads are however usually compatible across different
  linux distributions that use the same CPU architecture.
* The capture MariaDB instance must have binlogging enabled (`log-bin=1`)

## Capture

### Quick start

Workload capture can be used without definitions in static configuration files
and without a MaxScale restart.

If you have an existing routing service named, e.g., `RWS-Router` in your configuration
you can attach a capture filter to it dynamically:

```
maxctrl create filter CAPTURE_FLTR wcar
maxctrl link service RWS-Router CAPTURE_FLTR
```

You can then start a capture with
```
maxctrl call command wcar start CAPTURE_FLTR <options>
```
If limiting options were given the capture will stop automatically when one
of the limits is triggered. You can also stop the capture at any time with:
```
maxctrl call command wcar stop CAPTURE_FLTR
```

See [Replay](#Replay) to see how the captured files are used.

When capture is no longer needed you can remove it with:
```
maxctrl unlink service RWS-Router CAPTURE_FLTR
maxctrl destroy filter CAPTURE_FLTR
```

### File based configuration

Define a capture filter by adding the following
configuration object and add it to each service whose traffic is to be
captured. The traffic from all services that use the filter will be combined so
only use the filter in services that point to the same database cluster.

```
[CAPTURE_FLTR]
type=filter
module=wcar
capture_duration=1h # Limit capture duration to one hour
capture_size=1Gi    # Limit capture size to 1GiB
start_capture=true  # Start capturing immediately after starting MaxScale
```

### Example configuration file

Here is an example configuration for capturing from a single MariaDB server, where capture
starts when MaxScale starts and stops when MaxScale is stopped (`start_capture=true`).
MaxScale listens on port 4006 and connects to MariaDB on port 3306.

```
[server1]
type=server
address=127.0.0.1
port=3306

[MariaDB-Monitor]
type=monitor
module=mariadbmon
servers=server1
user=maxuser
password=maxpwd

[CAPTURE_FLTR]
type=filter
module=wcar
capture_duration=1h     # Limit capture duration to one hour
capture_size=1Gi        # Limit capture size to 1GiB
start_capture=true      # Start capturing immediately after starting MaxScale

[RWS-Router]
type=service
router=readwritesplit
cluster=MariaDB-Monitor
user=maxuser
password=maxpwd
filters=CAPTURE_FLTR

[RWS-Listener]
type=listener
service=RWS-Router
protocol=MariaDBClient
port=4006
```

### Capturing Traffic

This section explains how capture is done with configuration value `start_capture=true`.

Two things are needed to replay a workload: the client traffic that's captured
by MaxScale and a backup of the database that is used to initialize the replay
server. The backup should be taken from the point in time where the capture starts
and the simplest way to achieve this is to take a logical backup by doing the
following.

- Stop MaxScale
- Take a backup of the database with `mariadb-dump --all-databases --system=all`
- Start MaxScale

Once MaxScale has been started, the captured traffic will be written to files in
`/var/lib/maxscale/wcar/<name>` where `<name>` is the name of the filter (`CAPTURE_FLTR`
in the examples).

Each capture will generate a number of files named `NAME_YYYY-MM-DD_HHMMSS.SUFFIX`
where `NAME` is the capture name (defaults to `capture`), `YYYY-MM-DD` is the
date and `HHMMSS` is the time and the `SUFFIX` is one of `.cx`, `.ex` or
`.tx`. For example, a capture started on the 18th of April 2024 at 10:26:11
would generate a file named `capture_2024-04-18_102611.cx`.

### Stopping the Capture

To stop the capture, simply stop MaxScale, or issue the command:
```
maxctrl call command wcar stop CAPTURE_FLTR
```
where "CAPTURE_FLTR" is the name given to the filter as in the example configuration above.

To disable capturing altogether, remove the capture filter from the configuration and remove
it from all services that it was added to. Restart MaxScale.

If the replay is to take place on another server, the results can be collected
easily from `/var/lib/maxscale/wcar/` with the following command.

```
tar -caf captures.tar.gz -C /var/lib/maxscale wcar
```

Once the capture tarball has been generated, copy it to the replay server.
You might then want to delete the directories on the capture server from
/var/lib/maxscale/wcar/* to save space (and not copy them again later).

## Commands

Each of the commands can be called with the following syntax.

```
maxctrl call command wcar <command> <filter> [options]
```

The `<filter>` is the name of the filter instance. In the example configuration,
the value is `CAPTURE_FLTR`. The `[options]` is a list of optional arguments that
the command might expect.

### `start <filter> [options]`

Starts a new capture. Issuing a start command will stop any ongoing capture.

The start command supports optional key-value pairs. If the values are also defined in
the configuration file the command line options have priority. The supported keys are:
- **prefix** The prefix added to capture files. The default value is `capture`.
- **duration** Limit capture to this duration. See also configuration file value ['capture_duration'](#capture_duration).
- **size** Limit capture to approximately this many bytes in the file system. See also configuration file value ['capture_size'](capture_size).

The start command options are not persistent, and only apply to the capture that was thus started.

For example, starting a capture with the below command would create a capture
file named `Scenario1_2024-04-18_102605.cx` and limit the file system usage to approximately 10GiB.
If `capture_duration` was defined in the configuration file it would also be used.

If both duration and size are specified, the one that triggers first, stops the capture.

```
maxctrl call command wcar start CAPTURE_FLTR prefix=Scenario1 size=10G
```
Running the same command again, but without size=10G, the `capture_size` used would be that defined
in the configuration file or no limit if there was no such definition.

### `stop <filter>`

Stops the currently active capture if one is in progress.
```
maxctrl call command wcar stop CAPTURE_FLTR
```

## Replay

### Installation

Install the required packages on the MaxScale server where the replay is to be
done. An additional dependency that must be manually installed is Python,
version 3.7 or newer. On most linux distributions a new enough version is
available as the default Python interpreter.

The replay consists of restoring the database to the point in time where the
capture was started. Start by restoring the replay database to this state. Once
the database has been restored from the backup, copy the capture files over to
the replay MaxScale server.

### Preparing the Replay MariaDB Database

#### Full Restore

Start by restoring the database from the backup to put it at the point in time
where the capture was started. The GTID position of the first commit within the
capture can be seen in the output of the summary command:

```
maxplayer summary /path/to/capture.cx
```
If the captured data has not been transformed to replay format yet, the
command will perform the transformation before displaying the summary.

Run `maxplayer --help` to see the command line options. The help output
is also shown at the end of this file.

The replay also requires a user account using which the captured traffic is
replayed. This user must have access to all the tables in question. In practice
the simplest way to do this for testing is to create the user as follows:

```
CREATE USER 'maxreplay'@'%' IDENTIFIED BY 'replay-pw';
GRANT ALL ON *.* TO 'maxreplay'@'%';
```
#### Restore for read-only Replay

For captures that are intended for read-only Replay, it may not be as important
that the servers to be tested against are in the exact GTID the capture server was when
capture started. In fact, it may be advantageous that the servers are at the state after
the capture finished.

On the other hand, Replay also supports write-only. Following the Full Restore
procedure above and then running a write-only Replay prepares the replay server(s)
for easily running read-only multiple times. This way of running read-only may, for
example, be used when fine tuning server settings.

### Replaying the Capture

When replay is first done, the capture files will be transformed in-place.
Transform can be run separately as well. Depending on the size and structure
of the capture file, Transform can use up to twice the space of the capture.ex file.
The files with extension `.ex` contain most of the captured data (events).

Start by copying the replay file tarball created earlier (`captures.tar.gz`) to
the replay MaxScale server and copy it to a directory of your choice (here called
`/path/to/capture-dir`).
Then extract the files.
```
cd /path/to/capture-dir
tar -xaf captures.tar.gz
```

After this, replay the workload against the baseline MariaDB setup:

```
maxplayer replay --user maxreplay --password replay-pw --host <host:port> --output baseline-result.csv /path/to/capture.cx
```

Once the baseline replay results have been generated, run the replay again but
this time against the new MariaDB setup to which the baseline is compared to:

```
maxplayer replay --user maxreplay --password replay-pw --host <host:port> --output comparison-result.csv /path/to/capture.cx
```

After both replays have been completed, the results can be post-processed and visualized.

## Visualizing

The results of the captured replay must first be post-processed into summaries that
the visualization will then use. First, the `canonicals.csv` file must be
generated that is needed in the post-processing:

```
maxplayer canonicals /path/to/capture.cx > canonicals.csv
```

After that, the baseline and comparison replay results can be post-processed
into summaries using the `maxpostprocess` command:

```
maxpostprocess canonicals.csv baseline-result.csv -o baseline-summary.json
maxpostprocess canonicals.csv comparison-result.csv -o comparison-summary.json
```

The visualization itself is done with the `maxvisualize` program. The
visualization will open up a browser window to show the visualization. If no
browser opens up, the visualization URL is also printed into the command line
which by default should be `http://localhost:8866/`.

```
maxvisualize baseline-summary.json comparison-summary.json
```

## Settings

### `capture_dir`

- **Type**: path
- **Default**: /var/lib/maxscale/wcar/
- **Mandatory**: No
- **Dynamic**: No

Directory under which capture directories are stored. Each
capture directory has the name of the filter.
In the examples above the name "CAPTURE_FLTR" was used.

### `start_capture`

- **Type**: boolean
- **Default**: false
- **Mandatory**: No
- **Dynamic**: No

Start capture when maxscale starts.

### `capture_duration`

- **Type**: [duration](../Getting-Started/Configuration-Guide.md#durations)
- **Default**: 0s
- **Maximum**: Unlimited in MaxScale Enterprise, 5min in MaxScale Lite.
- **Mandatory**: No
- **Dynamic**: No

Limit capture to this duration. If set to zero there is no limit.

### `capture_size`

- **Type**: [size](../Getting-Started/Configuration-Guide.md#sizes)
- **Default**: 0
- **Maximum**: Unlimited in MaxScale Enterprise, 10MB in MaxScale Lite.
- **Mandatory**: No
- **Dynamic**: No

Limit capture to approximately this many bytes in the file system.
If set to zero there is no limit.

## maxplayer command line options

```
maxplayer -u user -p pwd --speed 1.5 -i 5s -o baseline.csv capture_2024-09-06_090002.cx --help
Usage: maxplayer [OPTION]... [COMMAND] FILE

Commands: (default: replay)
summary    Show a summary of the capture.
replay     Replay the capture.
convert    Converts the input file (either .cx or .rx) to a replay file (.rx or .csv).
canonicals List the canonical forms of the captured SQL as CSV.
dump-data  Dump capture data as SQL.
show       Show the SQL of one or more events.

Options:
--user          User name for login to the replay server.
-u              This version does not support using the actual user names
                that were used during capture.

--password      Only clear text passwords are supported as of yet.
-p

--host          The address of the replay server in <IP>:<port> format.
-h              E.g. 127.0.0.1:4006

--output        The name of the output file: e.g. baseline.csv.
-o

--report        Periodically report statistics of ongoing operations.
-r              The option takes a duration, such as 10s.

--report-file   The --report option by default writes to stdout.
-R              Provide the name of the file to write to. The file will
                be truncated every time it is written to, allowing for a
                simple status window by running 'watch cat <path-to-file>'
                in a terminal.

--speed         The value is a multiplier. 2.5 is 2.5x speed and 0.5 is half speed.
-s              A value of zero means no limit, or replay as fast as possible.
                A multiplier of 2.5 might not have any effect as the actual time spent
                depends on many factors, such as the captured volume and replay server.

--idle-wait     Relates to playback speed, and can be used together with --speed.
-i              During capture there can be long delays where there is no traffic.
                One hour of no capture traffic would mean replay waits for one hour.
                idle-wait allows to move simulation time forwards when such gaps
                occure. A 'gap' starts when all prior queries have fully executed.
                --idle-wait takes a duration value. A negative value turns the feature off,
                            i.e. the one hour wait would happen.
                --idle-wait 0s means time moves to the event start-time immediately
                            when a gap is detected, i.e., all gaps are skipped over.
                --idle-wait 10s means time moves to the event start-time 10 seconds
                            (wall time) after the gap was detected. Shorter
                            gaps than 10 seconds will thus be fully waited for.
                --idle-wait has a default value of 1 second.
                Examples: 1h, 60m, 3600s, 3600000ms, which all define the same duration.

--query-filter  Options: none, write-only, read-only. Default: none.
-f              Replay can optionally apply only writes or only reads. This option is useful
                once the databases to be tested have been prepared (see full documentation)
                and optionally either a write-only run, or a full replay has been run.
                Now multiple read-only runs against the server(s) are simple as no further
                data syncronization is needed.
                Note that this mode has its limitations as the query results may
                be very different than what they were during capture.

--analyze       Enabling this option will track the server Rows_read statistic for each query.
-A              This will slow down the overall replay time. The query time measurements
                are still valid, but currently this option should only be used when
                it is of real value to know how many rows the server read for each query.

--verbose       Verbose output. The option can be repeated for more verbosity: -vvv
-v

--version       Display the version number and copyrights.
-V

input file:       capture_2024-09-06_090002.cx
-h --help         true
-u --user         user
-p --password     pwd
-H --host         127.1.1.0:3306
-o --output       baseline.csv
-r --report       0ns
-R --report-file
-s --speed        1.5
-i --idle-wait    5s
-f --query-filter none
-A --analyze      false
-v --verbose      0
-V --version      0.2
```

## Limitations

- KILL commands do not work correctly during replay and may kill the wrong session ([MXS-5056](https://jira.mariadb.org/browse/MXS-5056))

- COM_STMT_BULK_EXECUTE is not captured ([MXS-5057](https://jira.mariadb.org/browse/MXS-5057))

- COM_STMT_EXECUTE that uses a cursor is replayed without a cursor ([MXS-5059](https://jira.mariadb.org/browse/MXS-5059))

  - For MyISAM and Aria tables, this will cause the table level lock to be held for a shorter time.

- Execution of a COM_STMT_SEND_LONG_DATA will not work ([MXS-5060](https://jira.mariadb.org/browse/MXS-5060))

- The capture files are not necessarily compatible with different linux distributions and CPU
  architectures than the original capture server has. Different combinations will require further
  testing, and once done, this document will be updated.
