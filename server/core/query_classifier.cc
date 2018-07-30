/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "internal/query_classifier.h"
#include <algorithm>
#include <unordered_map>
#include <maxscale/alloc.h>
#include <maxscale/atomic.h>
#include <maxscale/log_manager.h>
#include <maxscale/modutil.h>
#include <maxscale/pcre2.h>
#include <maxscale/platform.h>
#include <maxscale/utils.h>

#include "internal/modules.h"
#include "internal/trxboundaryparser.hh"

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

namespace
{

struct type_name_info
{
    const char* name;
    size_t name_len;
};

const char DEFAULT_QC_NAME[] = "qc_sqlite";
const char QC_TRX_PARSE_USING[] = "QC_TRX_PARSE_USING";

static struct this_unit
{
    QUERY_CLASSIFIER*    classifier;
    qc_trx_parse_using_t qc_trx_parse_using;
    qc_sql_mode_t        qc_sql_mode;
    int32_t              use_cached_result;
} this_unit =
{
    nullptr,
    QC_TRX_PARSE_USING_PARSER,
    QC_SQL_MODE_DEFAULT,
    1  // TODO: Make this configurable
};

class QCInfoCache;

static thread_local struct
{
    QCInfoCache* pInfo_cache;
} this_thread =
{
    nullptr
};


/**
 * @class QCInfoCache
 *
 * An instance of this class maintains a mapping from a canonical statement to
 * the QC_STMT_INFO object created by the actual query classifier.
 */
class QCInfoCache
{
public:
    QCInfoCache(const QCInfoCache&) = delete;
    QCInfoCache& operator=(const QCInfoCache&) = delete;

    QCInfoCache()
    {
    }

    ~QCInfoCache()
    {
        ss_dassert(this_unit.classifier);

        for (auto a : m_infos)
        {
            this_unit.classifier->qc_info_close(a.second.pInfo);
        }
    }

    QC_STMT_INFO* peek(const std::string& canonical_stmt) const
    {
        auto i = m_infos.find(canonical_stmt);

        return i != m_infos.end() ? i->second.pInfo : nullptr;
    }

    QC_STMT_INFO* get(const std::string& canonical_stmt)
    {
        QC_STMT_INFO* pInfo = nullptr;

        auto i = m_infos.find(canonical_stmt);

        if (i != m_infos.end())
        {
            const Entry& entry = i->second;

            if (entry.sql_mode == this_unit.qc_sql_mode)
            {
                ss_dassert(this_unit.classifier);
                this_unit.classifier->qc_info_dup(entry.pInfo);
                pInfo = entry.pInfo;
            }
            else
            {
                // If the sql_mode has changed, we discard the existing result.
                ss_dassert(this_unit.classifier);
                this_unit.classifier->qc_info_close(entry.pInfo);
                m_infos.erase(i);
            }
        }

        return pInfo;
    }

    void insert(const std::string& canonical_stmt, QC_STMT_INFO* pInfo)
    {
        ss_dassert(peek(canonical_stmt) == nullptr);
        ss_dassert(this_unit.classifier);

        this_unit.classifier->qc_info_dup(pInfo);

        m_infos.emplace(canonical_stmt, Entry(pInfo, this_unit.qc_sql_mode));
    }

    void erase(const std::string& canonical_stmt)
    {
        auto i = m_infos.find(canonical_stmt);
        ss_dassert(i != m_infos.end());

        if (i != m_infos.end())
        {
            QC_STMT_INFO* pInfo = i->second.pInfo;

            ss_dassert(this_unit.classifier);
            this_unit.classifier->qc_info_close(pInfo);

            m_infos.erase(i);
        }
    }

private:
    struct Entry
    {
        Entry(QC_STMT_INFO* pInfo, qc_sql_mode_t sql_mode)
            : pInfo(pInfo)
            , sql_mode(sql_mode)
        {
        }

        QC_STMT_INFO* pInfo;
        qc_sql_mode_t sql_mode;
    };

    typedef std::unordered_map<std::string, Entry> InfosByStmt;

    InfosByStmt m_infos;
};

bool use_cached_result()
{
    return atomic_load_int32(&this_unit.use_cached_result) != 0;
}

bool has_not_been_parsed(GWBUF* pStmt)
{
    // A GWBUF has not been parsed, if it does not have a parsing info object attached.
    return gwbuf_get_buffer_object_data(pStmt, GWBUF_PARSING_INFO) == nullptr;
}

void info_object_close(void* pData)
{
    ss_dassert(this_unit.classifier);
    this_unit.classifier->qc_info_close(static_cast<QC_STMT_INFO*>(pData));
}


/**
 * @class QCInfoCacheScope
 *
 * QCInfoCacheScope is somewhat like a guard or RAII class that
 * in the constructor
 * - figures out whether the query classification cache should be used,
 * - checks whether the classification result already exists, and
 * - if it does attaches it to the GWBUF
 * and in the destructor
 * - if the query classification result was not already present,
 *   stores the result it in the cache.
 */
class QCInfoCacheScope
{
public:
    QCInfoCacheScope(const QCInfoCacheScope&) = delete;
    QCInfoCacheScope& operator=(const QCInfoCacheScope&) = delete;

    QCInfoCacheScope(GWBUF* pStmt)
        : m_pStmt(pStmt)
    {
        if (use_cached_result() && has_not_been_parsed(m_pStmt))
        {
            char* zCanonical = qc_get_canonical(m_pStmt);

            if (zCanonical)
            {
                m_canonical = zCanonical;
                MXS_FREE(zCanonical);

                QC_STMT_INFO* pInfo = this_thread.pInfo_cache->get(m_canonical);

                if (pInfo)
                {
                    gwbuf_add_buffer_object(m_pStmt, GWBUF_PARSING_INFO, pInfo, info_object_close);
                    m_canonical.clear();
                }
            }
        }
    }

    ~QCInfoCacheScope()
    {
        if (!m_canonical.empty())
        {
            void* pData = gwbuf_get_buffer_object_data(m_pStmt, GWBUF_PARSING_INFO);
            ss_dassert(pData);
            QC_STMT_INFO* pInfo = static_cast<QC_STMT_INFO*>(pData);

            this_thread.pInfo_cache->insert(m_canonical, pInfo);
        }
    }

private:
    GWBUF*      m_pStmt;
    std::string m_canonical;
};

}


bool qc_setup(const QC_CACHE_PROPERTIES* cache_properties,
              qc_sql_mode_t sql_mode,
              const char* plugin_name, const char* plugin_args)
{
    QC_TRACE();
    ss_dassert(!this_unit.classifier);

    if (!plugin_name || (*plugin_name == 0))
    {
        MXS_NOTICE("No query classifier specified, using default '%s'.", DEFAULT_QC_NAME);
        plugin_name = DEFAULT_QC_NAME;
    }

    int32_t rv = QC_RESULT_ERROR;
    this_unit.classifier = qc_load(plugin_name);

    if (this_unit.classifier)
    {
        rv = this_unit.classifier->qc_setup(sql_mode, plugin_args);

        if (rv == QC_RESULT_OK)
        {
            this_unit.qc_sql_mode = sql_mode;

            bool use_cached_result = (cache_properties != nullptr);

            if (use_cached_result)
            {
                MXS_NOTICE("Query classification results are cached and reused.");
            }
            else
            {
                MXS_NOTICE("Query classification results are not cached.");
            }

            this_unit.use_cached_result = use_cached_result;
        }
        else
        {
            qc_unload(this_unit.classifier);
        }
    }

    return (rv == QC_RESULT_OK) ? true : false;
}

bool qc_process_init(uint32_t kind)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    const char* parse_using = getenv(QC_TRX_PARSE_USING);

    if (parse_using)
    {
        if (strcmp(parse_using, "QC_TRX_PARSE_USING_QC") == 0)
        {
            this_unit.qc_trx_parse_using = QC_TRX_PARSE_USING_QC;
            MXS_NOTICE("Transaction detection using QC.");
        }
        else if (strcmp(parse_using, "QC_TRX_PARSE_USING_PARSER") == 0)
        {
            this_unit.qc_trx_parse_using = QC_TRX_PARSE_USING_PARSER;
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
            rc = this_unit.classifier->qc_process_init() == 0;

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
    ss_dassert(this_unit.classifier);

    if (kind & QC_INIT_PLUGIN)
    {
        this_unit.classifier->qc_process_end();
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
    ss_dassert(this_unit.classifier);

    bool rc = false;

    this_thread.pInfo_cache = new (std::nothrow) QCInfoCache;

    if (this_thread.pInfo_cache)
    {
        rc = true;

        if (kind & QC_INIT_PLUGIN)
        {
            rc = this_unit.classifier->qc_thread_init() == 0;
        }

        if (!rc)
        {
            delete this_thread.pInfo_cache;
            this_thread.pInfo_cache = nullptr;
        }
    }

    return rc;
}

void qc_thread_end(uint32_t kind)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    if (kind & QC_INIT_PLUGIN)
    {
        this_unit.classifier->qc_thread_end();
    }

    delete this_thread.pInfo_cache;
    this_thread.pInfo_cache = nullptr;
}

qc_parse_result_t qc_parse(GWBUF* query, uint32_t collect)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    int32_t result = QC_QUERY_INVALID;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_parse(query, collect, &result);

    return (qc_parse_result_t)result;
}

uint32_t qc_get_type_mask(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    uint32_t type_mask = QUERY_TYPE_UNKNOWN;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_type_mask(query, &type_mask);

    return type_mask;
}

qc_query_op_t qc_get_operation(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    int32_t op = QUERY_OP_UNDEFINED;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_operation(query, &op);

    return (qc_query_op_t)op;
}

char* qc_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    char* name = NULL;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_created_table_name(query, &name);

    return name;
}

bool qc_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    int32_t is_drop_table = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_is_drop_table_query(query, &is_drop_table);

    return (is_drop_table != 0) ? true : false;
}

char** qc_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    char** names = NULL;
    *tblsize = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_table_names(query, fullnames, &names, tblsize);

    return names;
}

char* qc_get_canonical(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    char *rval;

    if (this_unit.classifier->qc_get_canonical)
    {
        this_unit.classifier->qc_get_canonical(query, &rval);
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
    ss_dassert(this_unit.classifier);

    int32_t has_clause = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_query_has_clause(query, &has_clause);

    return (has_clause != 0) ? true : false;
}

void qc_get_field_info(GWBUF* query, const QC_FIELD_INFO** infos, size_t* n_infos)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    *infos = NULL;

    uint32_t n = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_field_info(query, infos, &n);

    *n_infos = n;
}

void qc_get_function_info(GWBUF* query, const QC_FUNCTION_INFO** infos, size_t* n_infos)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    *infos = NULL;

    uint32_t n = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_function_info(query, infos, &n);

    *n_infos = n;
}

char** qc_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    char** names = NULL;
    *sizep = 0;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_database_names(query, &names, sizep);

    return names;
}

char* qc_get_prepare_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    char* name = NULL;

    QCInfoCacheScope scope(query);
    this_unit.classifier->qc_get_prepare_name(query, &name);

    return name;
}

GWBUF* qc_get_preparable_stmt(GWBUF* stmt)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    GWBUF* preparable_stmt = NULL;

    QCInfoCacheScope scope(stmt);
    this_unit.classifier->qc_get_preparable_stmt(stmt, &preparable_stmt);

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

    case QUERY_OP_LOAD_LOCAL:
        return "QUERY_OP_LOAD_LOCAL";

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

    case QUERY_TYPE_DEALLOC_PREPARE:
        {
            static const char name[] = "QUERY_TYPE_DEALLOC_PREPARE";
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
    QUERY_TYPE_DEALLOC_PREPARE,
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
    return qc_get_trx_type_mask_using(stmt, this_unit.qc_trx_parse_using);
}

void qc_set_server_version(uint64_t version)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    this_unit.classifier->qc_set_server_version(version);
}

uint64_t qc_get_server_version()
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    uint64_t version;

    this_unit.classifier->qc_get_server_version(&version);

    return version;
}

qc_sql_mode_t qc_get_sql_mode()
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    return this_unit.qc_sql_mode;
}

void qc_set_sql_mode(qc_sql_mode_t sql_mode)
{
    QC_TRACE();
    ss_dassert(this_unit.classifier);

    int32_t rv = this_unit.classifier->qc_set_sql_mode(sql_mode);
    ss_dassert(rv == QC_RESULT_OK);

    if (rv == QC_RESULT_OK)
    {
        this_unit.qc_sql_mode = sql_mode;
    }
}
