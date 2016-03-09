/**
 * @section LICENCE
 *
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is
 * free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab
 *
 * @file
 *
 */

#include <sqliteInt.h>
#include <log_manager.h>
#include <modinfo.h>
#include <platform.h>
#include <query_classifier.h>

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

static struct
{
    bool initialized;
} this_unit;

static thread_local struct
{
    bool initialized;
    sqlite3* db;
} this_thread;


static bool qc_sqlite_init(void);
static void qc_sqlite_end(void);
static bool qc_sqlite_thread_init(void);
static void qc_sqlite_thread_end(void);
static qc_query_type_t qc_sqlite_get_type(GWBUF* query);
static qc_query_op_t qc_sqlite_get_operation(GWBUF* query);
static char* qc_sqlite_get_created_table_name(GWBUF* query);
static bool qc_sqlite_is_drop_table_query(GWBUF* query);
static bool qc_sqlite_is_real_query(GWBUF* query);
static char** qc_sqlite_get_table_names(GWBUF* query, int* tblsize, bool fullnames);
static char* qc_sqlite_get_canonical(GWBUF* query);
static bool qc_sqlite_query_has_clause(GWBUF* query);
static char* qc_sqlite_get_qtype_str(qc_query_type_t qtype);
static char* qc_sqlite_get_affected_fields(GWBUF* query);
static char** qc_sqlite_get_database_names(GWBUF* query, int* sizep);


static bool qc_sqlite_init(void)
{
    QC_TRACE();
    assert(!this_unit.initialized);

    if (sqlite3_initialize() == 0)
    {
        this_unit.initialized = true;

        if (!qc_sqlite_thread_init())
        {
            this_unit.initialized = false;

            sqlite3_shutdown();
        }
    }
    else
    {
        MXS_ERROR("Failed to initialize sqlite3.");
    }

    return this_unit.initialized;
}

static void qc_sqlite_end(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);

    qc_sqlite_thread_end();

    sqlite3_shutdown();
    this_unit.initialized = false;
}

static bool qc_sqlite_thread_init(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(!this_thread.initialized);

    // TODO: It may be sufficient to have a single in-memory database for all threads.
    int rc = sqlite3_open(":memory:", &this_thread.db);
    if (rc == SQLITE_OK)
    {
        this_thread.initialized = true;

        MXS_INFO("In-memory sqlite database successfully opened for thread %lu.",
                 (unsigned long) pthread_self());
    }
    else
    {
        MXS_ERROR("Failed to open in-memory sqlite database for thread %lu: %d, %s",
                  (unsigned long) pthread_self(), rc, sqlite3_errstr(rc));
    }

    return this_thread.initialized;
}

static void qc_sqlite_thread_end(void)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    ss_dassert(this_thread.db);
    int rc = sqlite3_close(this_thread.db);

    if (rc != SQLITE_OK)
    {
        MXS_WARNING("The closing of the thread specific sqlite database failed: %d, %s",
                    rc, sqlite3_errstr(rc));
    }

    this_thread.db = NULL;
    this_thread.initialized = false;
}

static qc_query_type_t qc_sqlite_get_type(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_type not implemented yet.");

    return QUERY_TYPE_UNKNOWN;
}

static qc_query_op_t qc_sqlite_get_operation(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_operation not implemented yet.");

    return QUERY_OP_UNDEFINED;
}

static char* qc_sqlite_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_created_table_name not implemented yet.");

    return NULL;
}

static bool qc_sqlite_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_is_drop_table_query not implemented yet.");

    return false;
}

static bool qc_sqlite_is_real_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_is_real_query not implemented yet.");

    return false;
}

static char** qc_sqlite_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_table names not implemented yet.");

    return NULL;
}

static char* qc_sqlite_get_canonical(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_canonical not implemented yet.");

    return NULL;
}

static bool qc_sqlite_query_has_clause(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_query_has_clause not implemented yet.");

    return NULL;
}

static char* qc_sqlite_get_qtype_str(qc_query_type_t qtype)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_type_str not implemented yet.");

    return NULL;
}

static char* qc_sqlite_get_affected_fields(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_affected_files not implemented yet.");

    return NULL;
}

static char** qc_sqlite_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    ss_dassert(this_unit.initialized);
    ss_dassert(this_thread.initialized);

    MXS_ERROR("qc_sqlite: qc_get_database_names not implemented yet.");

    return NULL;
}

/**
 * EXPORTS
 */

static char version_string[] = "V1.0.0";

static QUERY_CLASSIFIER qc =
{
    qc_sqlite_init,
    qc_sqlite_end,
    qc_sqlite_thread_init,
    qc_sqlite_thread_end,
    qc_sqlite_get_type,
    qc_sqlite_get_operation,
    qc_sqlite_get_created_table_name,
    qc_sqlite_is_drop_table_query,
    qc_sqlite_is_real_query,
    qc_sqlite_get_table_names,
    qc_sqlite_get_canonical,
    qc_sqlite_query_has_clause,
    qc_sqlite_get_qtype_str,
    qc_sqlite_get_affected_fields,
    qc_sqlite_get_database_names,
};


MODULE_INFO info =
{
    MODULE_API_QUERY_CLASSIFIER,
    MODULE_IN_DEVELOPMENT,
    QUERY_CLASSIFIER_VERSION,
    "Query classifier using sqlite.",
};

char* version()
{
    return version_string;
}

void ModuleInit()
{
}

QUERY_CLASSIFIER* GetModuleObject()
{
    return &qc;
}
