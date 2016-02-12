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

#include <sys/auxv.h>
#include <elf.h>
#include <log_manager.h>
#include <modules.h>
#include <query_classifier.h>

//#define QC_TRACE_ENABLED
#undef QC_TRACE_ENABLED

#if defined(QC_TRACE_ENABLED)
#define QC_TRACE() MXS_NOTICE(__func__)
#else
#define QC_TRACE()
#endif

static const char QC_MYSQLEMBEDDED[] = "qc_mysqlembedded"; // The qc plugin we'll temporarily use.
static const char MAXPP[] = "maxpp";                       // The name of the plugin process executable.

static struct pp_self
{
    QUERY_CLASSIFIER* classifier;                // The classifier.
    char              classifier_name[PATH_MAX]; // The name of the classifier.
    pid_t             pp_pid;                    // The pid of the plugin process.
    char              pp_path[PATH_MAX];         // The path of the plugin process.
} *self = 0;


static QUERY_CLASSIFIER* load_and_init_classifier(const char* name)
{
    QUERY_CLASSIFIER* classifier = NULL;
    void* module = load_module(name, MODULE_QUERY_CLASSIFIER);

    if (module)
    {
        classifier = (QUERY_CLASSIFIER*) module;

        bool success = classifier->qc_init();

        if (success)
        {
            MXS_NOTICE("%s loaded and initialized.", name);
        }
        else
        {
            MXS_ERROR("Could not initialize %s.", name);
            unload_module(name);
            classifier = NULL;
        }
    }
    else
    {
        MXS_ERROR("Could not load %s.", name);
    }

    return classifier;
}

static void end_and_unload_classifier(QUERY_CLASSIFIER* classifier, const char* name)
{
    classifier->qc_end();
    unload_module(name);
}

static bool resolve_pp_path(char* path, int size)
{
    bool success = false;
    const char* exe_path = (const char*) getauxval(AT_EXECFN);
    size_t len = strlen(exe_path);

    if (len < size)
    {
        strcpy(path, exe_path);
        char* s = path + len;

        // Find the last '/'.
        while ((*s != '/') && (s != path))
        {
            --s;
            --len;
        }

        if (*s == '/')
        {
            ++s;
            ++len;
        }

        *s = 0;

        int required_size = len + sizeof(MAXPP) + 1;

        if (required_size <= size)
        {
            strcat(path, MAXPP);

            MXS_NOTICE("Path of plugin process executable: %s", path);
            success = true;
        }
        else
        {
            MXS_ERROR("The full path of the plugin process executable does "
                      "not fit into a buffer of %d bytes. ", size);
        }
    }
    else
    {
        MXS_ERROR("The full path of the current executable does not fit in a "
                  "buffer of %d bytes.", size);
    }

    return success;
}

static bool is_executable(const char* path)
{
    return access(path, X_OK) == 0;
}

static bool qc_pp_init(void)
{
    QC_TRACE();
    ss_dassert(!self);

    bool success = false;

    self = malloc(sizeof(*self));

    if (self)
    {
        memset(self, 0, sizeof(*self));

        const char* classifier_name = QC_MYSQLEMBEDDED;
        QUERY_CLASSIFIER* classifier = load_and_init_classifier(classifier_name);

        if (classifier)
        {
            char pp_path[PATH_MAX];

            if (resolve_pp_path(pp_path, sizeof(pp_path)))
            {
                if (is_executable(pp_path))
                {
                    self->classifier = classifier;
                    strcpy(self->classifier_name, classifier_name);
                    strcpy(self->pp_path, pp_path);
                    success = true;
                }
                else
                {
                    MXS_ERROR("%s does not exist or is not an executable.", pp_path);
                }
            }
            else
            {
                MXS_ERROR("Could not resolve the path of the plugin process executable. "
                          "Plugin process will not be launched.");
            }

            if (!success)
            {
                end_and_unload_classifier(classifier, classifier_name);
            }
        }

        if (!success)
        {
            free(self);
            self = NULL;
        }
    }
    else
    {
        MXS_ERROR("Out of memory.");
    }

    return success;
}

static void qc_pp_end(void)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    end_and_unload_classifier(self->classifier, self->classifier_name);
    free(self);
    self = NULL;
}

static bool qc_pp_thread_init(void)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_thread_init();
}

static void qc_pp_thread_end(void)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_thread_end();
}

static qc_query_type_t qc_pp_get_type(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_type(query);
}

static qc_query_op_t qc_pp_get_operation(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_operation(query);
}

static char* qc_pp_get_created_table_name(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_created_table_name(query);
}

static bool qc_pp_is_drop_table_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_is_drop_table_query(query);
}

static bool qc_pp_is_real_query(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_is_real_query(query);
}

static char** qc_pp_get_table_names(GWBUF* query, int* tblsize, bool fullnames)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_table_names(query, tblsize, fullnames);
}

static char* qc_pp_get_canonical(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_canonical(query);
}

static bool qc_pp_query_has_clause(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_query_has_clause(query);
}

static char* qc_pp_get_qtype_str(qc_query_type_t qtype)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_qtype_str(qtype);
}

static char* qc_pp_get_affected_fields(GWBUF* query)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_affected_fields(query);
}

static char** qc_pp_get_database_names(GWBUF* query, int* sizep)
{
    QC_TRACE();
    ss_dassert(self);
    ss_dassert(self->classifier);

    return self->classifier->qc_get_database_names(query, sizep);
}

/**
 * EXPORTS
 */

static char version_string[] = "V1.0.0";

static QUERY_CLASSIFIER qc =
{
    qc_pp_init,
    qc_pp_end,
    qc_pp_thread_init,
    qc_pp_thread_end,
    qc_pp_get_type,
    qc_pp_get_operation,
    qc_pp_get_created_table_name,
    qc_pp_is_drop_table_query,
    qc_pp_is_real_query,
    qc_pp_get_table_names,
    qc_pp_get_canonical,
    qc_pp_query_has_clause,
    qc_pp_get_qtype_str,
    qc_pp_get_affected_fields,
    qc_pp_get_database_names,
};


MODULE_INFO info =
{
    MODULE_API_QUERY_CLASSIFIER,
    MODULE_IN_DEVELOPMENT,
    QUERY_CLASSIFIER_VERSION,
    "Query classifier using external process.",
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
