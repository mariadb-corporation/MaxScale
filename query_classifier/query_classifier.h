#ifndef QUERY_CLASSIFIER_HG
#define QUERY_CLASSIFIER_HG
/*
This file is distributed as part of the MariaDB Corporation MaxScale. It is free
software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
version 2.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

Copyright MariaDB Corporation Ab

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
    /** Not implemented yet */
    //QUERY_TYPE_USERVAR_WRITE      = 0x000020, /*< Write a user variable:master or all */
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
    QUERY_OP_INSERT_SELECT = (1 << 4),
    QUERY_OP_TRUNCATE      = (1 << 5),
    QUERY_OP_ALTER_TABLE   = (1 << 6),
    QUERY_OP_CREATE_TABLE  = (1 << 7),
    QUERY_OP_CREATE_INDEX  = (1 << 8),
    QUERY_OP_DROP_TABLE    = (1 << 9),
    QUERY_OP_DROP_INDEX    = (1 << 10),
    QUERY_OP_CHANGE_DB     = (1 << 11),
    QUERY_OP_LOAD          = (1 << 12)
} qc_query_op_t;

#define QUERY_IS_TYPE(mask,type) ((mask & type) == type)

bool qc_init(const char* plugin_name);
void qc_end(void);

bool qc_thread_init(void);
void qc_thread_end(void);

/**
 * Create THD and use it for creating parse tree. Examine parse tree and
 * classify the query.
 */
qc_query_type_t qc_get_type(GWBUF* querybuf);
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

typedef struct query_classifier
{
    bool (*qc_init)(void);
    void (*qc_end)(void);

    bool (*qc_thread_init)(void);
    void (*qc_thread_end)(void);

    qc_query_type_t (*qc_get_type)(GWBUF* querybuf);
    qc_query_op_t (*qc_get_operation)(GWBUF* querybuf);

    char* (*qc_get_created_table_name)(GWBUF* querybuf);
    bool (*qc_is_drop_table_query)(GWBUF* querybuf);
    bool (*qc_is_real_query)(GWBUF* querybuf);
    char** (*qc_get_table_names)(GWBUF* querybuf, int* tblsize, bool fullnames);
    char* (*qc_get_canonical)(GWBUF* querybuf);
    bool (*qc_query_has_clause)(GWBUF* buf);
    char* (*qc_get_qtype_str)(qc_query_type_t qtype);
    char* (*qc_get_affected_fields)(GWBUF* buf);
    char** (*qc_get_database_names)(GWBUF* querybuf, int* size);
} QUERY_CLASSIFIER;

#define QUERY_CLASSIFIER_VERSION {1, 0, 0}

EXTERN_C_BLOCK_END

#endif
