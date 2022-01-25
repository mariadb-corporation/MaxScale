#if !defined(NOSQL_ROLE)
#error nosqlrole.hh cannot be included without NOSQL_ROLE being defined.
#endif

// *INDENT-OFF*
NOSQL_ROLE(BACKUP,                  (1 << 0),  "backup")
NOSQL_ROLE(CLUSTER_ADMIN,           (1 << 1),  "clusterAdmin")
NOSQL_ROLE(CLUSTER_MANAGER,         (1 << 2),  "clusterManager")
NOSQL_ROLE(CLUSTER_MONITOR,         (1 << 3),  "clusterMonitor")
NOSQL_ROLE(DB_ADMIN,                (1 << 4),  "dbAdmin")
NOSQL_ROLE(DB_ADMIN_ANY_DATABASE,   (1 << 5),  "dbAdminAnyDatabase")
NOSQL_ROLE(DB_OWNER,                (1 << 6),  "dbOwner")
NOSQL_ROLE(HOST_MANAGER,            (1 << 7),  "hostManager")
NOSQL_ROLE(READ_ANY_DATABASE,       (1 << 8),  "readAnyDatabase")
NOSQL_ROLE(READ,                    (1 << 9),  "read")
NOSQL_ROLE(READ_WRITE,              (1 << 10), "readWrite")
NOSQL_ROLE(READ_WRITE_ANY_DATABASE, (1 << 11), "readWriteAnyDatabase")
NOSQL_ROLE(RESTORE,                 (1 << 12), "restore")
NOSQL_ROLE(ROOT,                    (1 << 13), "root")
NOSQL_ROLE(USER_ADMIN,              (1 << 14), "userAdmin")
NOSQL_ROLE(USER_ADMIN_ANY_DATABASE, (1 << 15), "userAdminAnyDatabase")
// *INDENT-ON*
