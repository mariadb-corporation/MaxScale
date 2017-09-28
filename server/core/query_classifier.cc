/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxscale/query_classifier.h"
#include <maxscale/log_manager.h>
#include <maxscale/modutil.h>
#include <maxscale/alloc.h>
#include <maxscale/platform.h>
#include <maxscale/pcre2.h>
#include <maxscale/utils.h>
#include "maxscale/trxboundaryparser.hh"

#include "../core/maxscale/modules.h"

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

struct type_name_info
{
    const char* name;
    size_t name_len;
};

static const char DEFAULT_QC_NAME[] = "qc_sqlite";
static const char QC_TRX_PARSE_USING[] = "QC_TRX_PARSE_USING";

static QUERY_CLASSIFIER* classifier;

static qc_trx_parse_using_t qc_trx_parse_using = QC_TRX_PARSE_USING_PARSER;


bool qc_setup(const char* plugin_name, qc_sql_mode_t sql_mode, const char* plugin_args)
{
    QC_TRACE();
    ss_dassert(!classifier);

    if (!plugin_name || (*plugin_name == 0))
    {
        MXS_NOTICE("No query classifier specified, using default '%s'.", DEFAULT_QC_NAME);
        plugin_name = DEFAULT_QC_NAME;
    }

    int32_t rv = QC_RESULT_ERROR;
    classifier = qc_load(plugin_name);

    if (classifier)
    {
        rv = classifier->qc_setup(sql_mode, plugin_args);

        if (rv != QC_RESULT_OK)
        {
            qc_unload(classifier);
        }
    }

    return (rv == QC_RESULT_OK) ? true : false;
}

bool qc_process_init(uint32_t kind)
{
    QC_TRACE();
    ss_dassert(classifier);

    const char* parse_using = getenv(QC_TRX_PARSE_USING);

    if (parse_using)
    {
        if (strcmp(parse_using, "QC_TRX_PARSE_USING_QC") == 0)
        {
            qc_trx_parse_using = QC_TRX_PARSE_USING_QC;
            MXS_NOTICE("Transaction detection using QC.");
        }
        else if (strcmp(parse_using, "QC_TRX_PARSE_USING_PARSER") == 0)
        {
            qc_trx_parse_using = QC_TRX_PARSE_USING_PARSER;
            MXS_NOTICE("Transaction detection using custom PARSER.");
        }
        else
        {
            MXS_NOTICE("QC_TRX_PARSE_USING set, but the value %s is not known. "
                       "Parsing using QC.", parse_using);
        }
    }

    bool rc = qc_thread_init(QC_INIT_SELF);

    if (rc)
    {
        if (kind & QC_INIT_PLUGIN)
        {
            rc = classifier->qc_process_init() == 0;

            if (!rc)
            {
                qc_thread_end(QC_INIT_SELF);
            }
        }
    }

    return rc;
}

void qc_process_end(uint32_t kind)
{
    QC_TRACE();
    ss_dassert(classifier);

    if (kind & QC_INIT_PLUGIN)
    {
        classifier->qc_process_end();
    }

    qc_thread_end(QC_INIT_SELF);
}

QUERY_CLASSIFIER* qc_load(const char* plugin_name)
{
    void* module = load_module(plugin_name, MODULE_QUERY_CLASSIFIER);

    if (module)
    {
        MXS_INFO("%s loaded.", plugin_name);
    }
    else
    {
        MXS_ERROR("Could not load %s.", plugin_name);
    }

    return (QUERY_CLASSIFIER*) module;
}

void qc_unload(QUERY_CLASSIFIER* classifier)
{
    // TODO: The module loading/unloading needs an overhaul before we
    // TODO: actually can unload something.
    classifier = NULL;
}

bool qc_thread_init(uint32_t kind)
{
    QC_TRACE();
    ss_dassert(classifier);

    bool rc = true;

    if (kind & QC_INIT_PLUGIN)
    {
        rc = classifier->qc_thread_init() == 0;
    }

    return rc;
}

void qc_thread_end(uint32_t kind)
{
    QC_TRACE();
    ss_dassert(classifier);

    if (kind & QC_INIT_PLUGIN)
    {
        classifier->qc_thread_end();
    }
}

qc_parse_result_t qc_parse(GWBUF* query, uint32_t collect)
{
    QC_TRACE();
    ss_dassert(classifier);

    int32_t result = QC_QUERY_INVALID;

    classifier->qc_parse(query, collect, &result);

    return (qc_parse_result_t)result;
}

uint32_t qc_get_type_mask(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    uint32_t type_mask = QUERY_TYPE_UNKNOWN;

    classifier->qc_get_type_mask(query, &type_mask);

    return type_mask;
}

qc_query_op_t qc_get_operation(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    int32_t op = QUERY_OP_UNDEFINED;

    classifier->qc_get_operation(query, &op);

    return (qc_query_op_t)op;
}

char* qc_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    char* name = NULL;

    classifier->qc_get_created_table_name(query, &name);

    return name;
}

bool qc_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    int32_t is_drop_table = 0;

    classifier->qc_is_drop_table_query(query, &is_drop_table);

    return (is_drop_table != 0) ? true : false;
}

char** qc_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    ss_dassert(classifier);

    char** names = NULL;
    *tblsize = 0;

    classifier->qc_get_table_names(query, fullnames, &names, tblsize);

    return names;
}

char* qc_get_canonical(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    char *rval;

    if (classifier->qc_get_canonical)
    {
        classifier->qc_get_canonical(query, &rval);
    }
    else
    {
        rval = modutil_get_canonical(query);
    }

    if (rval)
    {
        squeeze_whitespace(rval);
    }

    return rval;
}

bool qc_query_has_clause(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    int32_t has_clause = 0;

    classifier->qc_query_has_clause(query, &has_clause);

    return (has_clause != 0) ? true : false;
}

void qc_get_field_info(GWBUF* query, const QC_FIELD_INFO** infos, size_t* n_infos)
{
    QC_TRACE();
    ss_dassert(classifier);

    *infos = NULL;

    uint32_t n = 0;

    classifier->qc_get_field_info(query, infos, &n);

    *n_infos = n;
}

void qc_get_function_info(GWBUF* query, const QC_FUNCTION_INFO** infos, size_t* n_infos)
{
    QC_TRACE();
    ss_dassert(classifier);

    *infos = NULL;

    uint32_t n = 0;

    classifier->qc_get_function_info(query, infos, &n);

    *n_infos = n;
}

char** qc_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    ss_dassert(classifier);

    char** names = NULL;
    *sizep = 0;

    classifier->qc_get_database_names(query, &names, sizep);

    return names;
}

char* qc_get_prepare_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    char* name = NULL;

    classifier->qc_get_prepare_name(query, &name);

    return name;
}

GWBUF* qc_get_preparable_stmt(GWBUF* stmt)
{
    QC_TRACE();
    ss_dassert(classifier);

    GWBUF* preparable_stmt = NULL;

    classifier->qc_get_preparable_stmt(stmt, &preparable_stmt);

    return preparable_stmt;
}

const char* qc_op_to_string(qc_query_op_t op)
{
    switch (op)
    {
    case QUERY_OP_UNDEFINED:
        return "QUERY_OP_UNDEFINED";

    case QUERY_OP_ALTER:
        return "QUERY_OP_ALTER";

    case QUERY_OP_CALL:
        return "QUERY_OP_CALL";

    case QUERY_OP_CHANGE_DB:
        return "QUERY_OP_CHANGE_DB";

    case QUERY_OP_CREATE:
        return "QUERY_OP_CREATE";

    case QUERY_OP_DELETE:
        return "QUERY_OP_DELETE";

    case QUERY_OP_DROP:
        return "QUERY_OP_DROP";

    case QUERY_OP_EXPLAIN:
        return "QUERY_OP_EXPLAIN";

    case QUERY_OP_GRANT:
        return "QUERY_OP_GRANT";

    case QUERY_OP_INSERT:
        return "QUERY_OP_INSERT";

    case QUERY_OP_LOAD:
        return "QUERY_OP_LOAD";

    case QUERY_OP_REVOKE:
        return "QUERY_OP_REVOKE";

    case QUERY_OP_SELECT:
        return "QUERY_OP_SELECT";

    case QUERY_OP_SHOW:
        return "QUERY_OP_SHOW";

    case QUERY_OP_TRUNCATE:
        return "QUERY_OP_TRUNCATE";

    case QUERY_OP_UPDATE:
        return "QUERY_OP_UPDATE";

    default:
        return "UNKNOWN_QUERY_OP";
    }
}

struct type_name_info type_to_type_name_info(qc_query_type_t type)
{
    struct type_name_info info;

    switch (type)
    {
    case QUERY_TYPE_UNKNOWN:
        {
            static const char name[] = "QUERY_TYPE_UNKNOWN";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_LOCAL_READ:
        {
            static const char name[] = "QUERY_TYPE_LOCAL_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READ:
        {
            static const char name[] = "QUERY_TYPE_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_WRITE:
        {
            static const char name[] = "QUERY_TYPE_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_MASTER_READ:
        {
            static const char name[] = "QUERY_TYPE_MASTER_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SESSION_WRITE:
        {
            static const char name[] = "QUERY_TYPE_SESSION_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_USERVAR_WRITE:
        {
            static const char name[] = "QUERY_TYPE_USERVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_USERVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_USERVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SYSVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_SYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    /** Not implemented yet */
    //case QUERY_TYPE_SYSVAR_WRITE:
    case QUERY_TYPE_GSYSVAR_READ:
        {
            static const char name[] = "QUERY_TYPE_GSYSVAR_READ";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_GSYSVAR_WRITE:
        {
            static const char name[] = "QUERY_TYPE_GSYSVAR_WRITE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_BEGIN_TRX:
        {
            static const char name[] = "QUERY_TYPE_BEGIN_TRX";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_ENABLE_AUTOCOMMIT:
        {
            static const char name[] = "QUERY_TYPE_ENABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_DISABLE_AUTOCOMMIT:
        {
            static const char name[] = "QUERY_TYPE_DISABLE_AUTOCOMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_ROLLBACK:
        {
            static const char name[] = "QUERY_TYPE_ROLLBACK";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_COMMIT:
        {
            static const char name[] = "QUERY_TYPE_COMMIT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_PREPARE_NAMED_STMT:
        {
            static const char name[] = "QUERY_TYPE_PREPARE_NAMED_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_PREPARE_STMT:
        {
            static const char name[] = "QUERY_TYPE_PREPARE_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_EXEC_STMT:
        {
            static const char name[] = "QUERY_TYPE_EXEC_STMT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_CREATE_TMP_TABLE:
        {
            static const char name[] = "QUERY_TYPE_CREATE_TMP_TABLE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_READ_TMP_TABLE:
        {
            static const char name[] = "QUERY_TYPE_READ_TMP_TABLE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SHOW_DATABASES:
        {
            static const char name[] = "QUERY_TYPE_SHOW_DATABASES";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QUERY_TYPE_SHOW_TABLES:
        {
            static const char name[] = "QUERY_TYPE_SHOW_TABLES";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    default:
        {
            static const char name[] = "UNKNOWN_QUERY_TYPE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;
    }

    return info;
}


const char* qc_type_to_string(qc_query_type_t type)
{
    return type_to_type_name_info(type).name;
}

static const qc_query_type_t QUERY_TYPES[] =
{
    /* Excluded by design */
    //QUERY_TYPE_UNKNOWN,
    QUERY_TYPE_LOCAL_READ,
    QUERY_TYPE_READ,
    QUERY_TYPE_WRITE,
    QUERY_TYPE_MASTER_READ,
    QUERY_TYPE_SESSION_WRITE,
    QUERY_TYPE_USERVAR_WRITE,
    QUERY_TYPE_USERVAR_READ,
    QUERY_TYPE_SYSVAR_READ,
    /** Not implemented yet */
    //QUERY_TYPE_SYSVAR_WRITE,
    QUERY_TYPE_GSYSVAR_READ,
    QUERY_TYPE_GSYSVAR_WRITE,
    QUERY_TYPE_BEGIN_TRX,
    QUERY_TYPE_ENABLE_AUTOCOMMIT,
    QUERY_TYPE_DISABLE_AUTOCOMMIT,
    QUERY_TYPE_ROLLBACK,
    QUERY_TYPE_COMMIT,
    QUERY_TYPE_PREPARE_NAMED_STMT,
    QUERY_TYPE_PREPARE_STMT,
    QUERY_TYPE_EXEC_STMT,
    QUERY_TYPE_CREATE_TMP_TABLE,
    QUERY_TYPE_READ_TMP_TABLE,
    QUERY_TYPE_SHOW_DATABASES,
    QUERY_TYPE_SHOW_TABLES,
};

static const int N_QUERY_TYPES = sizeof(QUERY_TYPES) / sizeof(QUERY_TYPES[0]);
static const int QUERY_TYPE_MAX_LEN = 29; // strlen("QUERY_TYPE_PREPARE_NAMED_STMT");

char* qc_typemask_to_string(uint32_t types)
{
    int len = 0;

    // First calculate how much space will be needed.
    for (int i = 0; i < N_QUERY_TYPES; ++i)
    {
        if (types & QUERY_TYPES[i])
        {
            if (len != 0)
            {
                ++len; // strlen("|");
            }

            len += QUERY_TYPE_MAX_LEN;
        }
    }

    ++len;

    // Then make one allocation and build the string.
    char* s = (char*) MXS_MALLOC(len);

    if (s)
    {
        if (len > 1)
        {
            char* p = s;

            for (int i = 0; i < N_QUERY_TYPES; ++i)
            {
                qc_query_type_t type = QUERY_TYPES[i];

                if (types & type)
                {
                    if (p != s)
                    {
                        strcpy(p, "|");
                        ++p;
                    }

                    struct type_name_info info = type_to_type_name_info(type);

                    strcpy(p, info.name);
                    p += info.name_len;
                }
            }
        }
        else
        {
            *s = 0;
        }
    }

    return s;
}

static uint32_t qc_get_trx_type_mask_using_qc(GWBUF* stmt)
{
    uint32_t type_mask = qc_get_type_mask(stmt);

    if (qc_query_is_type(type_mask, QUERY_TYPE_WRITE) &&
        qc_query_is_type(type_mask, QUERY_TYPE_COMMIT))
    {
        // This is a commit reported for "CREATE TABLE...",
        // "DROP TABLE...", etc. that cause an implicit commit.
        type_mask = 0;
    }
    else
    {
        // Only START TRANSACTION can be explicitly READ or WRITE.
        if (!(type_mask & QUERY_TYPE_BEGIN_TRX))
        {
            // So, strip them away for everything else.
            type_mask &= ~(QUERY_TYPE_WRITE | QUERY_TYPE_READ);
        }

        // Then leave only the bits related to transaction and
        // autocommit state.
        type_mask &= (QUERY_TYPE_BEGIN_TRX |
                      QUERY_TYPE_WRITE |
                      QUERY_TYPE_READ |
                      QUERY_TYPE_COMMIT |
                      QUERY_TYPE_ROLLBACK |
                      QUERY_TYPE_ENABLE_AUTOCOMMIT |
                      QUERY_TYPE_DISABLE_AUTOCOMMIT);
    }

    return type_mask;
}

static uint32_t qc_get_trx_type_mask_using_parser(GWBUF* stmt)
{
    maxscale::TrxBoundaryParser parser;

    return parser.type_mask_of(stmt);
}

uint32_t qc_get_trx_type_mask_using(GWBUF* stmt, qc_trx_parse_using_t use)
{
    uint32_t type_mask = 0;

    switch (use)
    {
    case QC_TRX_PARSE_USING_QC:
        type_mask = qc_get_trx_type_mask_using_qc(stmt);
        break;

    case QC_TRX_PARSE_USING_PARSER:
        type_mask = qc_get_trx_type_mask_using_parser(stmt);
        break;

    default:
        ss_dassert(!true);
    }

    return type_mask;
}

uint32_t qc_get_trx_type_mask(GWBUF* stmt)
{
    return qc_get_trx_type_mask_using(stmt, qc_trx_parse_using);
}

void qc_set_server_version(uint64_t version)
{
    QC_TRACE();
    ss_dassert(classifier);

    classifier->qc_set_server_version(version);
}

uint64_t qc_get_server_version()
{
    QC_TRACE();
    ss_dassert(classifier);

    uint64_t version;

    classifier->qc_get_server_version(&version);

    return version;
}

qc_sql_mode_t qc_get_sql_mode()
{
    QC_TRACE();
    ss_dassert(classifier);

    qc_sql_mode_t sql_mode;

    ss_debug(int32_t rv = ) classifier->qc_get_sql_mode(&sql_mode);
    ss_dassert(rv == QC_RESULT_OK);

    return sql_mode;
}

void qc_set_sql_mode(qc_sql_mode_t sql_mode)
{
    QC_TRACE();
    ss_dassert(classifier);

    ss_debug(int32_t rv = ) classifier->qc_set_sql_mode(sql_mode);
    ss_dassert(rv == QC_RESULT_OK);
}
