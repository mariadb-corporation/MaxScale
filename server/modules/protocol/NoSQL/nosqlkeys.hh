/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

namespace nosql
{

namespace key
{

const char ADMIN_ONLY[]                      = "adminOnly";
const char ALL_PLANS_EXECUTION[]             = "allPlansExecution";
const char ARGV[]                            = "argv";
const char ASSERTS[]                         = "asserts";
const char BATCH_SIZE[]                      = "batchSize";
const char BITS[]                            = "bits";
const char CAPPED[]                          = "capped";
const char CLIENT[]                          = "client";
const char CODE_NAME[]                       = "codeName";
const char CODE[]                            = "code";
const char COLLECTION[]                      = "collection";
const char COMMANDS[]                        = "commands";
const char COMMAND[]                         = "command";
const char COMPILED[]                        = "compiled";
const char CONFIG[]                          = "config";
const char CONNECTION_ID[]                   = "connectionId";
const char CONNECTIONS[]                     = "connections";
const char CONVERSATION_ID[]                 = "conversationId";
const char CPU_ADDR_SIZE[]                   = "cpuAddrSize";
const char CPU_ARCH[]                        = "cpuArch";
const char CREATED_COLLECTION_AUTOMATICALLY[]= "createdCollectionAutomatically";
const char CURRENT_TIME[]                    = "currentTime";
const char CURSORS[]                         = "cursors";
const char CURSORS_ALIVE[]                   = "cursorsAlive";
const char CURSORS_KILLED[]                  = "cursorsKilled";
const char CURSORS_NOT_FOUND[]               = "cursorsNotFound";
const char CURSORS_UNKNOWN[]                 = "cursorsUnknown";
const char CURSOR[]                          = "cursor";
const char DB[]                              = "db";
const char DATABASES[]                       = "databases";
const char DEACH[]                           = "$each";
const char DEBUG[]                           = "debug";
const char DELETES[]                         = "deletes";
const char DIRECTION[]                       = "direction";
const char DOCUMENTS[]                       = "documents";
const char DONE[]                            = "done";
const char DROPPED[]                         = "dropped";
const char DROP_TARGET[]                     = "dropTarget";
const char ELECTION_METRICS[]                = "electionMetrics";
const char EMPTY[]                           = "empty";
const char ERRMSG[]                          = "errmsg";
const char ERROR[]                           = "error";
const char ERRORS[]                          = "errors";
const char ERR[]                             = "err";
const char EXECUTION_STATS[]                 = "executionStats";
const char EXECUTION_SUCCESS[]               = "executionSuccess";
const char EXTRA_INDEX_ENTRIES[]             = "extraIndexEntries";
const char EXTRA_INFO[]                      = "extraInfo";
const char EXTRA[]                           = "extra";
const char FIELDS[]                          = "fields";
const char FILTER[]                          = "filter";
const char FIRST_BATCH[]                     = "firstBatch";
const char FLOW_CONTROL[]                    = "flowControl";
const char GIT_VERSION[]                     = "gitVersion";
const char HELP[]                            = "help";
const char HOST[]                            = "host";
const char HOSTNAME[]                        = "hostname";
const char ID[]                              = "id";
const char ID_INDEX[]                        = "idIndex";
const char INDEX_DETAILS[]                   = "indexDetails";
const char INDEX[]                           = "index";
const char INDEX_FILTER_SET[]                = "indexFilterSet";
const char INDEXES[]                         = "indexes";
const char INFO[]                            = "info";
const char INPROG[]                          = "inprog";
const char ISMASTER[]                        = "ismaster";
const char JAVASCRIPT_ENGINE[]               = "javascriptEngine";
const char KEY_PATTERN[]                     = "keyPattern";
const char KEY_VALUE[]                       = "keyValue";
const char KEY[]                             = "key";
const char KEYS_PER_INDEX[]                  = "keysPerIndex";
const char KIND[]                            = "kind";
const char LAST_ERROR_OBJECT[]               = "lastErrorObject";
const char LIMIT[]                           = "limit";
const char LOCAL_TIME[]                      = "localTime";
const char LOGICAL_SESSION_TIMEOUT_MINUTES[] = "logicalSessionTimeoutMinutes";
const char LOG[]                             = "log";
const char MARIADB[]                         = "mariadb";
const char MAX[]                             = "max";
const char MAX_BSON_OBJECT_SIZE[]            = "maxBsonObjectSize";
const char MAX_MESSAGE_SIZE_BYTES[]          = "maxMessageSizeBytes";
const char MAXSCALE[]                        = "maxscale";
const char MAX_WIRE_VERSION[]                = "maxWireVersion";
const char MAX_WRITE_BATCH_SIZE[]            = "maxWriteBatchSize";
const char MECHANISM[]                       = "mechanism";
const char MECHANISMS[]                      = "mechanisms";
const char MEM_LIMIT_MB[]                    = "memLimitMB";
const char MEM_SIZE_MB[]                     = "memSizeMB";
const char MESSAGE[]                         = "message";
const char MIN[]                             = "min";
const char MIN_WIRE_VERSION[]                = "minWireVersion";
const char MISSING_INDEX_ENTRIES[]           = "missingIndexEntries";
const char MODULES[]                         = "modules";
const char MULTI[]                           = "multi";
const char NAMES[]                           = "names";
const char NAME[]                            = "name";
const char NAME_ONLY[]                       = "nameOnly";
const char NEW[]                             = "new";
const char NEXT_BATCH[]                      = "nextBatch";
const char NRECORDS[]                        = "nrecords";
const char NS[]                              = "ns";
const char NUMA_ENABLED[]                    = "numaEnabled";
const char NUM_CORES[]                       = "numCores";
const char N[]                               = "n";
const char N_INDEXES[]                       = "nIndexes";
const char N_INDEXES_WAS[]                   = "nIndexesWas";
const char N_INVALID_DOCUMENTS[]             = "nInvalidDocuments";
const char N_MODIFIED[]                      = "nModified";
const char N_RETURNED[]                      = "nReturned";
const char OK[]                              = "ok";
const char OPENSSL[]                         = "openssl";
const char OPTIONS[]                         = "options";
const char ORDERBY[]                         = "orderby";
const char ORDERED[]                         = "ordered";
const char OS[]                              = "os";
const char PARSED[]                          = "parsed";
const char PARSED_QUERY[]                    = "parsedQuery";
const char PAYLOAD[]                         = "payload";
const char PID[]                             = "pid";
const char PLANNER_VERSION[]                 = "plannerVersion";
const char PORT[]                            = "port";
const char PROJECTION[]                      = "projection";
const char PWD[]                             = "pwd";
const char QUERY[]                           = "query";
const char QUERY_PLANNER[]                   = "queryPlanner";
const char Q[]                               = "q";
const char READ_ONLY[]                       = "readOnly";
const char REJECTED_PLANS[]                  = "rejectedPlans";
const char REMOVE[]                          = "remove";
const char RESPONSE[]                        = "response";
const char REQUIRES_AUTH[]                   = "requiresAuth";
const char ROLE[]                            = "role";
const char ROLES[]                           = "roles";
const char RUNNING[]                         = "running";
const char SASL_SUPPORTED_MECHS[]            = "saslSupportedMechs";
const char SERVER_INFO[]                     = "serverInfo";
const char SINGLE_BATCH[]                    = "singleBatch";
const char SIZE_ON_DISK[]                    = "sizeOnDisk";
const char SKIP[]                            = "skip";
const char SLAVE_OK[]                        = "slaveOk";
const char SORT[]                            = "sort";
const char SQL[]                             = "sql";
const char STORAGE_ENGINE[]                  = "storageEngine";
const char STAGE[]                           = "stage";
const char STATE[]                           = "state";
const char STORAGE_ENGINES[]                 = "storageEngines";
const char SYNC_MILLIS[]                     = "syncMillis";
const char SYSTEM[]                          = "system";
const char TOPOLOGY_VERSION[]                = "topologyVersion";
const char TOTAL_LINES_WRITTEN[]             = "totalLinesWritten";
const char TOTAL_SIZE[]                      = "totalSize";
const char TYPE[]                            = "type";
const char UPDATED_EXISTING[]                = "updatedExisting";
const char UPDATE[]                          = "update";
const char UPDATES[]                         = "updates";
const char UPSERT[]                          = "upsert";
const char UPSERTED[]                        = "upserted";
const char UPTIME[]                          = "uptime";
const char UPTIME_ESTIMATE[]                 = "uptimeEstimate";
const char UPTIME_MILLIS[]                   = "uptimeMillis";
const char USERS[]                           = "users";
const char USER[]                            = "user";
const char USER_ID[]                         = "userId";
const char U[]                               = "u";
const char V[]                               = "v";
const char VALID[]                           = "valid";
const char VALUE[]                           = "value";
const char VERBOSITY[]                       = "verbosity";
const char VERSION_ARRAY[]                   = "versionArray";
const char VERSION[]                         = "version";
const char VIEW_ON[]                         = "viewOn";
const char WARNINGS[]                        = "warnings";
const char WAS[]                             = "was";
const char WINNING_PLAN[]                    = "winningPlan";
const char WRITE_CONCERN[]                   = "writeConcern";
const char WRITE_ERRORS[]                    = "writeErrors";
const char WRITTEN_TO[]                      = "writtenTo";
const char WTIMEOUT[]                        = "wtimeout";
const char W[]                               = "w";
const char YOU[]                             = "you";
const char _ID[]                             = "_id";
const char _ID_[]                            = "_id_";

}

}
