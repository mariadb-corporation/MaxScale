# MaxScale and SSL

MaxScale supports client side SSL connections. Enabling is done on a per service basis and each service has its own set of certificates.

## SSL Options

Here are the options which relate to SSL and certificates.
Parameter|Values     |Description
---------|-----------|--------
ssl         | disabled, enabled, required |`disable` disables SSL, `enabled` enables SSL for client connections but still allows non-SSL connections and `required` requires SSL from all client connections. With the `required` option, client connections that do not use SSL will be rejected.
ssl_cert    | path to file              |Path to server certificate
ssl_key     | path to file              |Path to server private key
ssl_ca_cert | path to file              |Path to Certificate Authority file
ssl_version|SSLV3,TLSV10,TLSV11,TLSV12,MAX| The SSL method level,  defaults to highest available encryption level which is TLSv1.2
ssl_cert_verify_depth|integer|Certificate authority certificate verification depth, default is 100.
