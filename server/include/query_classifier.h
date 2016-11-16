#ifndef QUERY_CLASSIFIER_HG
#define QUERY_CLASSIFIER_HG
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <skygw_utils.h>
#include <buffer.h>

EXTERN_C_BLOCK_BEGIN

typedef enum
{
    QUERY_TYPE_UNKNOWN            = 0x000000, /*< Initial value, can't be tested bitwisely */
    QUERY_TYPE_LOCAL_READ         = 0x000001, /*< Read non-database data, execute in MaxScale:any */
    QUERY_TYPE_READ               = 0x000002, /*< Read database data:any */
    QUERY_TYPE_WRITE              = 0x000004, /*< Master data will be  modified:master */
    QUERY_TYPE_MASTER_READ        = 0x000008, /*< Read from the master:master */
    QUERY_TYPE_SESSION_WRITE      = 0x000010, /*< Session data will be modified:master or all */
    QUERY_TYPE_USERVAR_WRITE      = 0x000020, /*< Write a user variable:master or all */
    QUERY_TYPE_USERVAR_READ       = 0x000040, /*< Read a user variable:master or any */
    QUERY_TYPE_SYSVAR_READ        = 0x000080, /*< Read a system variable:master or any */
    /** Not implemented yet */
    //QUERY_TYPE_SYSVAR_WRITE       = 0x000100, /*< Write a system variable:master or all */
    QUERY_TYPE_GSYSVAR_READ       = 0x000200, /*< Read global system variable:master or any */
    QUERY_TYPE_GSYSVAR_WRITE      = 0x000400, /*< Write global system variable:master or all */
    QUERY_TYPE_BEGIN_TRX          = 0x000800, /*< BEGIN or START TRANSACTION */
    QUERY_TYPE_ENABLE_AUTOCOMMIT  = 0x001000, /*< SET autocommit=1 */
    QUERY_TYPE_DISABLE_AUTOCOMMIT = 0x002000, /*< SET autocommit=0 */
    QUERY_TYPE_ROLLBACK           = 0x004000, /*< ROLLBACK */
    QUERY_TYPE_COMMIT             = 0x008000, /*< COMMIT */
    QUERY_TYPE_PREPARE_NAMED_STMT = 0x010000, /*< Prepared stmt with name from user:all */
    QUERY_TYPE_PREPARE_STMT       = 0x020000, /*< Prepared stmt with id provided by server:all */
    QUERY_TYPE_EXEC_STMT          = 0x040000, /*< Execute prepared statement:master or any */
    QUERY_TYPE_CREATE_TMP_TABLE   = 0x080000, /*< Create temporary table:master (could be all) */
    QUERY_TYPE_READ_TMP_TABLE     = 0x100000, /*< Read temporary table:master (could be any) */
    QUERY_TYPE_SHOW_DATABASES     = 0x200000, /*< Show list of databases */
    QUERY_TYPE_SHOW_TABLES        = 0x400000  /*< Show list of tables */
} qc_query_type_t;

typedef enum
{
    QUERY_OP_UNDEFINED     = 0,
    QUERY_OP_SELECT        = (1 << 0),
    QUERY_OP_UPDATE        = (1 << 1),
    QUERY_OP_INSERT        = (1 << 2),
    QUERY_OP_DELETE        = (1 << 3),
    QUERY_OP_TRUNCATE      = (1 << 4),
    QUERY_OP_ALTER         = (1 << 5),
    QUERY_OP_CREATE        = (1 << 6),
    QUERY_OP_DROP          = (1 << 7),
    QUERY_OP_CHANGE_DB     = (1 << 8),
    QUERY_OP_LOAD          = (1 << 9),
    QUERY_OP_GRANT         = (1 << 10),
    QUERY_OP_REVOKE        = (1 << 11)
} qc_query_op_t;

typedef enum qc_parse_result
{
    QC_QUERY_INVALID          = 0, /*< The query was not recognized or could not be parsed. */
    QC_QUERY_TOKENIZED        = 1, /*< The query was classified based on tokens; incompletely classified. */
    QC_QUERY_PARTIALLY_PARSED = 2, /*< The query was only partially parsed; incompletely classified. */
    QC_QUERY_PARSED           = 3  /*< The query was fully parsed; completely classified. */
} qc_parse_result_t;

#define QUERY_IS_TYPE(mask,type) ((mask & type) == type)

bool qc_init(const char* plugin_name, const char* plugin_args);
void qc_end(void);

typedef struct query_classifier QUERY_CLASSIFIER;

QUERY_CLASSIFIER* qc_load(const char* plugin_name);
void qc_unload(QUERY_CLASSIFIER* classifier);

bool qc_thread_init(void);
void qc_thread_end(void);

qc_parse_result_t qc_parse(GWBUF* querybuf);

uint32_t qc_get_type(GWBUF* querybuf);
qc_query_op_t qc_get_operation(GWBUF* querybuf);

char* qc_get_created_table_name(GWBUF* querybuf);
bool qc_is_drop_table_query(GWBUF* querybuf);
bool qc_is_real_query(GWBUF* querybuf);
char** qc_get_table_names(GWBUF* querybuf, int* tblsize, bool fullnames);
char* qc_get_canonical(GWBUF* querybuf);
bool qc_query_has_clause(GWBUF* buf);
char* qc_get_qtype_str(qc_query_type_t qtype);
char* qc_get_affected_fields(GWBUF* buf);
char** qc_get_database_names(GWBUF* querybuf, int* size);

const char* qc_op_to_string(qc_query_op_t op);
const char* qc_type_to_string(qc_query_type_t type);
char* qc_types_to_string(uint32_t types);

struct query_classifier
{
    bool (*qc_init)(const char* args);
    void (*qc_end)(void);

    bool (*qc_thread_init)(void);
    void (*qc_thread_end)(void);

    qc_parse_result_t (*qc_parse)(GWBUF* querybuf);

    uint32_t (*qc_get_type)(GWBUF* querybuf);
    qc_query_op_t (*qc_get_operation)(GWBUF* querybuf);

    char* (*qc_get_created_table_name)(GWBUF* querybuf);
    bool (*qc_is_drop_table_query)(GWBUF* querybuf);
    bool (*qc_is_real_query)(GWBUF* querybuf);
    char** (*qc_get_table_names)(GWBUF* querybuf, int* tblsize, bool fullnames);
    char* (*qc_get_canonical)(GWBUF* querybuf);
    bool (*qc_query_has_clause)(GWBUF* buf);
    char* (*qc_get_affected_fields)(GWBUF* buf);
    char** (*qc_get_database_names)(GWBUF* querybuf, int* size);
};

#define QUERY_CLASSIFIER_VERSION {1, 0, 0}

EXTERN_C_BLOCK_END

#endif
