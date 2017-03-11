/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
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

typedef struct qc_trx_regex
{
    const char* match;
    uint32_t    type_mask;
    pcre2_code* code;
} QC_TRX_REGEX;

static QC_TRX_REGEX qc_trx_regexes[] =
{
    {
        "^\\s*BEGIN(\\s+WORK)?\\s*;?\\s*$",
        QUERY_TYPE_BEGIN_TRX
    },
    {
        "^\\s*COMMIT(\\s+WORK)?\\s*;?\\s*$",
        QUERY_TYPE_COMMIT,
    },
    {
        "^\\s*ROLLBACK(\\s+WORK)?\\s*;?\\s*$",
        QUERY_TYPE_ROLLBACK
    },
    {
        "^\\s*START\\s+TRANSACTION\\s+READ\\s+ONLY\\s*;?\\s*$",
        QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_READ
    },
    {
        "^\\s*START\\s+TRANSACTION\\s+READ\\s+WRITE\\s*;?\\s*$",
        QUERY_TYPE_BEGIN_TRX | QUERY_TYPE_WRITE
    },
    {
        "^\\s*START\\s+TRANSACTION(\\s*;?\\s*|(\\s+.*))$",
        QUERY_TYPE_BEGIN_TRX
    },
    {
        "^\\s*SET\\s+AUTOCOMMIT\\s*\\=\\s*(1|true)\\s*;?\\s*$",
        QUERY_TYPE_COMMIT|QUERY_TYPE_ENABLE_AUTOCOMMIT
    },
    {
        "^\\s*SET\\s+AUTOCOMMIT\\s*\\=\\s*(0|false)\\s*;?\\s*$",
        QUERY_TYPE_BEGIN_TRX|QUERY_TYPE_DISABLE_AUTOCOMMIT
    }
};

#define N_TRX_REGEXES (sizeof(qc_trx_regexes) / sizeof(qc_trx_regexes[0]))

static thread_local pcre2_match_data* qc_trx_thread_datas[N_TRX_REGEXES];

static qc_trx_parse_using_t qc_trx_parse_using = QC_TRX_PARSE_USING_QC;

static bool compile_trx_regexes();
static bool create_trx_thread_datas();
static void free_trx_regexes();
static void free_trx_thread_datas();

static bool compile_trx_regexes()
{
    QC_TRX_REGEX* regex = qc_trx_regexes;
    QC_TRX_REGEX* end = regex + N_TRX_REGEXES;

    bool success = true;

    while (success && (regex < end))
    {
        int errcode;
        PCRE2_SIZE erroffset;
        regex->code = pcre2_compile((PCRE2_SPTR)regex->match, PCRE2_ZERO_TERMINATED, PCRE2_CASELESS,
                                    &errcode, &erroffset, NULL);

        if (!regex->code)
        {
            success = false;
            PCRE2_UCHAR errbuf[512];
            pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));

            MXS_ERROR("Regex compilation failed at %lu for regex '%s': %s.",
                      erroffset, regex->match, errbuf);
        }

        ++regex;
    }

    if (!success)
    {
        free_trx_regexes();
    }

    return success;
}

static void free_trx_regexes()
{
    QC_TRX_REGEX* begin = qc_trx_regexes;
    QC_TRX_REGEX* regex = begin + N_TRX_REGEXES;

    while (regex > begin)
    {
        --regex;

        if (regex->code)
        {
            pcre2_code_free(regex->code);
            regex->code = NULL;
        }
    }
}

static bool create_trx_thread_datas()
{
    bool success = true;

    QC_TRX_REGEX* regex = qc_trx_regexes;
    QC_TRX_REGEX* end = regex + N_TRX_REGEXES;

    pcre2_match_data** data = qc_trx_thread_datas;

    while (success && (regex < end))
    {
        *data = pcre2_match_data_create_from_pattern(regex->code, NULL);

        if (!*data)
        {
            success = false;
            MXS_ERROR("PCRE2 match data creation failed.");
        }

        ++regex;
        ++data;
    }

    if (!success)
    {
        free_trx_thread_datas();
    }

    return success;
}

static void free_trx_thread_datas()
{
    pcre2_match_data** begin = qc_trx_thread_datas;
    pcre2_match_data** data = begin + N_TRX_REGEXES;

    while (data > begin)
    {
        --data;

        if (*data)
        {
            pcre2_match_data_free(*data);
            *data = NULL;
        }
    }
}


bool qc_setup(const char* plugin_name, const char* plugin_args)
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
        rv = classifier->qc_setup(plugin_args);

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
        else if (strcmp(parse_using, "QC_TRX_PARSE_USING_REGEX") == 0)
        {
            qc_trx_parse_using = QC_TRX_PARSE_USING_REGEX;
            MXS_NOTICE("Transaction detection using REGEX.");
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

    bool rc = compile_trx_regexes();

    if (rc)
    {
        rc = qc_thread_init(QC_INIT_SELF);

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
        else
        {
            free_trx_regexes();
        }
    }
    else
    {
        MXS_ERROR("Could not compile transaction regexes.");
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

    free_trx_regexes();
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

    bool rc = create_trx_thread_datas();

    if (rc)
    {
        if (kind & QC_INIT_PLUGIN)
        {
            rc = classifier->qc_thread_init() == 0;

            if (!rc)
            {
                free_trx_thread_datas();
                rc = false;
            }
        }
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

    free_trx_thread_datas();
}

qc_parse_result_t qc_parse(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(classifier);

    int32_t result = QC_QUERY_INVALID;

    classifier->qc_parse(query, &result);

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

struct type_name_info field_usage_to_type_name_info(qc_field_usage_t usage)
{
    struct type_name_info info;

    switch (usage)
    {
    case QC_USED_IN_SELECT:
        {
            static const char name[] = "QC_USED_IN_SELECT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QC_USED_IN_SUBSELECT:
        {
            static const char name[] = "QC_USED_IN_SUBSELECT";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QC_USED_IN_WHERE:
        {
            static const char name[] = "QC_USED_IN_WHERE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QC_USED_IN_SET:
        {
            static const char name[] = "QC_USED_IN_SET";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    case QC_USED_IN_GROUP_BY:
        {
            static const char name[] = "QC_USED_IN_GROUP_BY";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;

    default:
        {
            static const char name[] = "UNKNOWN_FIELD_USAGE";
            info.name = name;
            info.name_len = sizeof(name) - 1;
        }
        break;
    }

    return info;
}



const char* qc_field_usage_to_string(qc_field_usage_t usage)
{
    return field_usage_to_type_name_info(usage).name;
}

static const qc_field_usage_t FIELD_USAGE_VALUES[] =
{
    QC_USED_IN_SELECT,
    QC_USED_IN_SUBSELECT,
    QC_USED_IN_WHERE,
    QC_USED_IN_SET,
    QC_USED_IN_GROUP_BY,
};

static const int N_FIELD_USAGE_VALUES =
    sizeof(FIELD_USAGE_VALUES) / sizeof(FIELD_USAGE_VALUES[0]);
static const int FIELD_USAGE_MAX_LEN = 20; // strlen("QC_USED_IN_SUBSELECT");

char* qc_field_usage_mask_to_string(uint32_t mask)
{
    size_t len = 0;

    // First calculate how much space will be needed.
    for (int i = 0; i < N_FIELD_USAGE_VALUES; ++i)
    {
        if (mask & FIELD_USAGE_VALUES[i])
        {
            if (len != 0)
            {
                ++len; // strlen("|");
            }

            len += FIELD_USAGE_MAX_LEN;
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

            for (int i = 0; i < N_FIELD_USAGE_VALUES; ++i)
            {
                qc_field_usage_t value = FIELD_USAGE_VALUES[i];

                if (mask & value)
                {
                    if (p != s)
                    {
                        strcpy(p, "|");
                        ++p;
                    }

                    struct type_name_info info = field_usage_to_type_name_info(value);

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

const char* qc_op_to_string(qc_query_op_t op)
{
    switch (op)
    {
    case QUERY_OP_UNDEFINED:
        return "QUERY_OP_UNDEFINED";

    case QUERY_OP_SELECT:
        return "QUERY_OP_SELECT";

    case QUERY_OP_UPDATE:
        return "QUERY_OP_UPDATE";

    case QUERY_OP_INSERT:
        return "QUERY_OP_INSERT";

    case QUERY_OP_DELETE:
        return "QUERY_OP_DELETE";

    case QUERY_OP_TRUNCATE:
        return "QUERY_OP_TRUNCATE";

    case QUERY_OP_ALTER:
        return "QUERY_OP_ALTER";

    case QUERY_OP_CREATE:
        return "QUERY_OP_CREATE";

    case QUERY_OP_DROP:
        return "QUERY_OP_DROP";

    case QUERY_OP_CHANGE_DB:
        return "QUERY_OP_CHANGE_DB";

    case QUERY_OP_LOAD:
        return "QUERY_OP_LOAD";

    case QUERY_OP_GRANT:
        return "QUERY_OP_GRANT";

    case QUERY_OP_REVOKE:
        return "QUERY_OP_REVOKE";

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

    if (!(type_mask & QUERY_TYPE_BEGIN_TRX))
    {
        type_mask &= ~(QUERY_TYPE_WRITE | QUERY_TYPE_READ);
    }

    type_mask &= (QUERY_TYPE_BEGIN_TRX |
                  QUERY_TYPE_WRITE |
                  QUERY_TYPE_READ |
                  QUERY_TYPE_COMMIT |
                  QUERY_TYPE_ROLLBACK |
                  QUERY_TYPE_ENABLE_AUTOCOMMIT |
                  QUERY_TYPE_DISABLE_AUTOCOMMIT);

    return type_mask;
}

static uint32_t qc_get_trx_type_mask_using_regex(GWBUF* stmt)
{
    uint32_t type_mask = 0;

    char* sql;
    int len;

    // This will exclude prepared statement but we are fine with that.
    if (modutil_extract_SQL(stmt, &sql, &len))
    {
        QC_TRX_REGEX* regex = qc_trx_regexes;
        QC_TRX_REGEX* end = regex + N_TRX_REGEXES;
        pcre2_match_data** data = qc_trx_thread_datas;

        while ((type_mask == 0) && (regex < end))
        {
            if (pcre2_match(regex->code, (PCRE2_SPTR)sql, len, 0, 0, *data, NULL) >= 0)
            {
                type_mask = regex->type_mask;
            }

            ++regex;
            ++data;
        }
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

    case QC_TRX_PARSE_USING_REGEX:
        type_mask = qc_get_trx_type_mask_using_regex(stmt);
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
