# Throttle

This filter was added in MariaDB MaxScale 2.3

## Overview

The throttle filter replaces and extends on the limit_queries functionality of 
[the Database Firewall filter](./Database-Firewall-Filter.md).

The throttle filter is used to limit the maximum query frequency (QPS - queries 
per second) of a database session to a configurable value. The main use cases 
are to prevent a rogue session (client side error) and a DoS attack from 
overloading the system.

The throttling is dynamic. The query frequency is not limited to an absolute 
value. Depending on the configuration the throttle will allow some amount of 
high frequency queries, or especially short bursts with no frequency limitation.

## Configuration

### Basic Configuration
```
[Throttle]
type = filter
module = throttlefilter
max_qps = 500
throttling_duration = 60000
...

[Routing Service]
type = service
filters = Throttle
```

This configuration states that the query frequency will be throttled to around 
500 qps, and that the time limit a query is allowed to stay at the maximum 
frequency is 60 seconds. All values involving time are configured in 
milliseconds. With the basic configuration the throttling will be nearly 
immediate, i.e. a session will only be allowed very short bursts of high 
frequency querying.

When a session has been continuously throttled for `throttling_duration` 
milliseconds, or 60 seconds in this example, MaxScale will disconnect the 
session.

### Allowing high frequency bursts

The two parameters `max_qps` and `sampling_duration` together define how a 
session is throttled.

Suppose max qps is 400 qps and sampling duration is 10 seconds. Since QPS is not 
an instantaneous measure, but one could say it has a granularity of 10 seconds, 
we see that over the 10 seconds 10*400 = 4000 queries are allowed before 
throttling kicks in.

With these values, a fresh session can start off with a speed of 2000 qps, and 
maintain that speed for 2 seconds before throttling starts.

If the client continues to query at high speed and throttling duration is set to 
10 seconds, Maxscale will disconnect the session 12 seconds after it started.

### Filter Parameters

#### `max_qps`

_Maximum queries per second_. Required parameter.

This is the frequency to which a session will be limited over a given time 
period. QPS is not measured as an instantaneous value but over a configurable 
sampling duration (see `sampling_duration`).

#### `throttling_duration`

Required parameter. Time in milliseconds.

This defines how long a session is allowed to be throttled before MaxScale 
disconnects the session.

### `sampling_duration`

Optional parameter. Default 250 milliseconds.

Sampling duration defines the window of time over which QPS is measured. This 
parameter directly affects the amount of time that high frequency queries are 
allowed before throttling kicks in.

The lower this value is, the more strict throttling becomes. Converserly, the 
longer this time is, the longer bursts of high frequency querying is allowed.

Due to the underlying granularity of time measurment (as of June 2018), it is 
not recommended that this value is set to less than 100 milliseconds.

### `continuous_duration`

Optional parameter. Default 2000 milliseconds or 2 seconds.

This value defines what continuous throttling means. Continuous throttling 
starts as soon as the filter throttles the frequency. Continuous throttling ends 
when no throttling has been performed in the past `continuous_duration` time.
