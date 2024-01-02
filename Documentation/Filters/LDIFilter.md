# LDI Filter

[TOC]

The `ldi` (LOAD DATA INFILE) filter was introduced in MaxScale 23.08.0 and it
extends the MariaDB `LOAD DATA INFILE` syntax to support loading data from any
object storage that supports the S3 API. This includes cloud offerings like AWS
S3 and Google Cloud Storage as well as locally run services like Minio.

If the filename starts with either `S3://` or `gs://`, the path is interpreted
as a S3 object file. The prefix is case-insensitive. For example, the following
command would load the file `my-data.csv` from the bucket `my-bucket` into the
table `t1`.

```sql
LOAD DATA INFILE 'S3://my-bucket/my-data.csv' INTO TABLE t1
    FIELDS TERMINATED BY ',' LINES TERMINATED BY '\n';
```

## How to Upload Data

Here is a minimal configuration for the filter that can be used to load data
from AWS S3:

```
[LDI-Filter]
type=filter
module=ldi
host=s3.amazonaws.com
region=us-east-1
```

The first step is to move the file to be loaded into the same region that
MaxScale and the MariaDB servers are in. One factor in the speed of the upload
is the network latency and minimizing it by moving the source and the
destination closer improves the data loading speed.

The next step is to connect to MaxScale and prepare the session for an upload by
providing the service account access and secret keys.

```sql
SET @maxscale.ldi.s3_key='<my-access-key>', @maxscale.ldi.s3_secret='<my-secret-key>';
```

Once the credentials are configured, the data loading can be started:

```sql
LOAD DATA INFILE 'S3://my-bucket/my-data.csv' INTO TABLE t1;
```

### Data Uploads with MariaDB Xpand

This feature has been removed in MaxScale 24.02.

## Common Problems With Data Loading

### Missing Files

If you are using self-hosted object storage programs like Minio, a common
problem is that they do not necessarily support the newer virtual-hosted-style
requests that is used by AWS. This usually manifests as an error either about a
missing file or a missing bucket.

If the `host` parameter is set to a hostname, it's assumed that the object
storage supports the newer virtual-hosted-style requests. If this not the case,
the filter must be configured with `protocol_version=1`.

Conversely, if the `host` parameter is set to a plain IP address, it is assumed
that it does not support the newer virtual-hosted-style request. If the host
does support it, the filter must be configured with `protocol_version=2`.

## Configuration Parameters

### `key`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes

The S3 access key used to perform all requests to it.

This must be either configured in the MaxScale configuration file or set with
`SET @maxscale.ldi.s3_key='<key>'` before starting the data load.

### `secret`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes

The S3 secret key used to perform all requests to it.

This must be either configured in the MaxScale configuration file or set with
`SET @maxscale.ldi.s3_secret='<secret>'` before starting the data load.

### `region`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `us-east-1`

The S3 region where the data is located.

The value can be overridden with `SET @maxscale.ldi.s3_region='<region>'` before
starting the data load.

### `host`

- **Type**: string
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: `s3.amazonaws.com`

The location of the S3 object storage. By default the original AWS S3 host is
used. The corresponding value for Google Cloud Storage is
`storage.googleapis.com`.

The value can be overridden with `SET @maxscale.ldi.s3_host='<host>'` before
starting the data load.

### `port`

- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0

The port on which the S3 object storage is listening. If unset or set to the
value of 0, the default S3 port is used.

The value can be overridden with `SET @maxscale.ldi.s3_port=<port>` before
starting the data load. Note that unlike the other values, the value for this
variable must be an SQL integer and not an SQL string.

### `no_verify`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

If set to true, TLS certificate verification for the object storage is skipped.

### `use_http`

- **Type**: [boolean](../Getting-Started/Configuration-Guide.md#booleans)
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: false

If set to true, communication with the object storage is done unencrypted using
HTTP instead of HTTPS.

### `protocol_version`

- **Type**: integer
- **Mandatory**: No
- **Dynamic**: Yes
- **Default**: 0
- **Values**: 0, 1, 2

Which protocol version to use. By default the protocol version is derived from
the value of `host` but this automatic protocol version deduction will not
always produce the correct result. For the legacy path-style requests used by
older S3 storage buckets, the value must be set to 1. All new buckets use the
protocol version 2.

For object storage programs like Minio, the value must be set to 1 as the bucket
name cannot be resolved via the subdomain like it is done for object stores in
the cloud.

### `import_user`

This parameter has been removed in MaxScale 24.02.

### `import_password`

This parameter has been removed in MaxScale 24.02.
