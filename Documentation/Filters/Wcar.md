# WCAR: Workload Capture and Replay

**NOTE** The WCAR filter is only available in _MaxScale Enterprise_.

The _WCAR_ filter captures client traffic and stores it in a replayable format.

The filter is designed for capturing traffic on a production MaxScale
instance. The captured data can then be used as a reproducible way of generating
accurate client traffic without having to write application-specific traffic
generators.

The captured workloads can be used to verify that upgrades of MariaDB behave as
expected and to measure what effects configuration changes may have.

Another use case is to find out why certain scenarios take much longer then
expected, a kind of sql debugging.

[TOC]

## Prerequisites

- Both the capture MaxScale and replay MaxScale servers must use the same
  operating system and CPU architecture. For example, if the capture was taken
  on an x86_64 RHEL 8 instance, the replay should also happen on an x86_64 RHEL
  8 instance. Captured workloads may be compatible across operating system
  versions that use the same CPU architecture.

- The capture MariaDB instance must have binlogging enabled (`log-bin=1`)

# Capture

## Installation

To start capturing the workload, define the WCAR filter by adding the following
configuration object and add it to each service whose traffic is going to be
captured. The traffic from all services that use the filter will be combined so
only use the filter in services that point to the same database cluster.

```
[WCAR]
type=filter
module=wcar
# Limit capture duration to one hour
capture_duration=1h
# Limit capture size to 1GiB
capture_size=1Gi
# Start capturing immediately after starting MaxScale
start_capture=true
```

If capture is used dynamically, using WCAR Commands (command line using maxctrl).
The simplest config is:
```
[WCAR]
type=filter
module=wcar
```

## Example configuration

Here is an example configuration for capturing from a single MariaDB
server. MaxScale listens on port 4006 and connects to MariaDB on port 3306.

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

[RWS-Router]
type=service
router=readwritesplit
cluster=MariaDB-Monitor
user=maxuser
password=maxpwd
filters=WCAR

[WCAR]
type=filter
module=wcar
# Limit capture duration to one hour
capture_duration=1h
# Limit capture size to 1GiB
capture_size=1Gi
# Start capturing immediately after starting MaxScale
start_capture=true

[RWS-Listener]
type=listener
service=RWS-Router
protocol=MariaDBClient
port=4006
```

## Capturing Traffic

This first section explains how capture is done with configuration value `start_capture=true`.
But note that capture can be started and stopped dynamically using module commands as well
(where the configuration value can be`start_capture=false`, or omitted as that is the default).

Two things are needed to replay a workload: the client traffic that's captured
by MaxScale and a backup of the database that is used to initialize the replay
server. The backup must be taken from the point in time where the capture starts
and the simplest way to achieve this is to take a logical backup by doing the
following.

- Stop MaxScale

- Take a backup of the database with `mariadb-dump --all-databases --system=all`

- Start MaxScale

Once MaxScale has been started, the captured traffic will be written to files in
`/var/lib/maxscale/wcar/<name>` where `<name>` is the name of the filter (`WCAR`
in the examples).

Each capture will generate a number files named `NAME_YYYY-MM-DD_HHMMSS.SUFFIX`
where `NAME` is the capture name (defaults to `capture`), `YYYY-MM-DD` is the
date and `HHMMSS` is the time and the `SUFFIX` is one of `.cx`, `.ex` or
`.gx`. For example, a capture started on the 18th of April 2024 at 10:26:11
would generate a file named `capture_2024-04-18_102611.cx`.

## Stopping the Capture

To stop the capture, simply stop MaxScale. To disable capturing, remove the WCAR
filter from the configuration and remove it from all services that it was added
to.

If the replay is to take place on another server, the results can be collected
easily from `/var/lib/maxscale/wcar/` with the following command.

```
tar -caf captures.tar.gz -C /var/lib/maxscale/ wcar
```

Once the capture tarball has been generated, copy it to the replay server.

# WCAR Commands

Each of the commands can be called with the following syntax.

```
maxctrl call command wcar <command> <filter> [options]
```

The `<filter>` is the name of the filter instance. In the example configuration,
the value would be `WCAR`. The `[options]` is a list of optional arguments that
the command might expect.

## `start <filter> [options]`

Starts a new capture. Issuing a start command will stop any ongoing capture.

The start command supports optional key-value pairs. If the values are defined in
the configuration file they are used instead. The supported keys are:
* `prefix` The prefix added to capture files. The default value is "capture".
* `duration` Limit capture to this duration. See also configuration file value `capture_duration`.
* `size` Limit capture to approximately this many bytes in the file system. See also configuration file value `capture_size`.

For example, starting a capture with the following command would create a capture
file named `my-capture_2024-04-18_102605.cx` and limit the file system usage to approximately 10GiB.
If `capture_duration` was defined in the configuration file it would also be used.

```
maxctrl call command wcar start WCAR prefix=my-capture size=10G
```
Running the same command anew, but without size=10G, the capture_size used would be that defined in the configuration file or
no limit if there was no such definition.

If both duration and size are specified, the one that triggers first, stops the capture.

## `stop <filter>`

Stops the currently active capture if one is in progress.

# Replay

## Installation

Install the required packages on the MaxScale server where the replay is to be
done. An additional dependency that must be manually installed is Python,
version 3.7 or newer. On most operating systems a new enough version is
available as the default Python interpreter.

The replay consists of restoring the database to the point in time where the
capture was started. Start by restoring the replay database to this state. Once
the database has been restored from the backup, copy the capture files over to
the replay MaxScale server.

## Preparing the Replay MariaDB Database

Start by restoring the database from the backup to put it at the point in time
where the capture was started. The GTID position at which the capture starts can
be seen in the output of the summary command:

```
maxplayer summary /path/to/capture.cx
```

The replay also requires a user account using which the captured traffic is
replayed. This user must have access to all the tables in question. In practice
the simplest way to do this for testing is to create the user as follows:

```
CREATE USER 'maxreplay'@'%' IDENTIFIED BY 'replay-pw';
GRANT ALL ON *.* TO 'maxreplay'@'%';
```

## Replaying the Capture

Start by copying the replay file tarball created earlier (`captures.tar.gz`) to
the replay MaxScale server and extracting them to some location.

After this, replay the workload against the baseline MariaDB setup:

```
maxplayer replay --user maxreplay --password replay-pw --host <host:port> --csv --output baseline-result.csv /path/to/capture.cx
```

Once the baseline replay results have been generated, run the replay again but
this time against the new MariaDB setup to which the baseline is compared to:

```
maxplayer replay --user maxreplay --password replay-pw --host <host:port> --csv --output comparison-result.csv /path/to/capture.cx
```

After both replays have been completed, the results can be post-processed and visualized.

# Visualizing

The results of the WCAR replay must first be post-processed into summaries that
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

# WCAR Parameters

## `capture_dir`

- **Type**: path
- **Default**: /var/lib/maxscale/capture/
- **Mandatory**: No
- **Dynamic**: No

Directory where capture files are stored.

## `start_capture`

- **Type**: boolean
- **Default**: false
- **Mandatory**: No
- **Dynamic**: No

Start capture when maxscale starts.

## `capture_duration`

- **Type**: duration
- **Default**: 0s
- **Mandatory**: No
- **Dynamic**: No

Limit capture to this duration.

## `capture_size`

- **Type**: size
- **Default**: 0
- **Mandatory**: No
- **Dynamic**: No

Limit capture to approximately this many bytes in the file system.

# Limitations

- Pipelined execution of SQL is not supported ([MXS-5054](https://jira.mariadb.org/browse/MXS-5054))

- The original username of sessions is not captured ([MXS-5053](https://jira.mariadb.org/browse/MXS-5053))

- COM_CHANGE_USER and COM_RESET_CONNECTION are not captured ([MXS-5055](https://jira.mariadb.org/browse/MXS-5055))

- KILL commands do not work correctly during replay and may kill the wrong session ([MXS-5056](https://jira.mariadb.org/browse/MXS-5056))

- COM_STMT_BULK_EXECUTE is not captured ([MXS-5057](https://jira.mariadb.org/browse/MXS-5057))

- COM_STMT_EXECUTE that uses a cursor is replayed without a cursor ([MXS-5059](https://jira.mariadb.org/browse/MXS-5059))

  - For MyISAM and Aria tables, this will cause the table level lock to be held for a shorter time.

- Execution of a COM_STMT_SEND_LONG_DATA will not work ([MXS-5060](https://jira.mariadb.org/browse/MXS-5060))

- The capture files are not compatible with different operating systems and CPU
  architectures than the original capture server. This is a limitation of
  boost::archive::binary_oarchive.
