CREATE ROLE proxy_authenticator;
GRANT SELECT ON mysql.user TO proxy_authenticator;
GRANT SELECT ON mysql.db TO proxy_authenticator;
GRANT SELECT ON mysql.tables_priv TO proxy_authenticator;
GRANT SHOW DATABASES ON *.* TO proxy_authenticator;
CREATE ROLE proxy_monitor;
GRANT REPLICATION CLIENT ON *.* TO proxy_monitor;
