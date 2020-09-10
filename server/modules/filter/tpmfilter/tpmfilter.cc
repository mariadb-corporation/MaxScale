/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file tpmfilter.c - Transaction Performance Monitoring Filter
 * @verbatim
 *
 * A simple filter that groups queries into a transaction with the latency.
 *
 * The filter reads the routed queries, groups them into a transaction by
 * detecting 'commit' statement at the end. The transactions are timestamped with a
 * unix-timestamp and the latency of a transaction is recorded in milliseconds.
 * The filter will not record transactions that are rolled back.
 * Please note that the filter only works with 'autocommit' option disabled.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 *
 * Optional parameters:
 *  filename=<name of the file to which transaction performance logs are written (default=tpm.log)>
 *  delimiter=<delimiter for columns in a log (default=':::')>
 *  query_delimiter=<delimiter for query statements in a transaction (default='@@@')>
 *  source=<source address to limit filter>
 *  user=<username to limit filter>
 *
 * Date         Who             Description
 * 06/12/2015   Dong Young Yoon Initial implementation
 * 14/12/2016   Dong Young Yoon Prints which server the query executed on
 *
 * @endverbatim
 */

#define MXS_MODULE_NAME "tpmfilter"

#include <maxscale/ccdefs.hh>

#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <regex.h>
#include <thread>

#include <maxbase/alloc.h>
#include <maxscale/filter.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/server.hh>
#include <maxbase/atomic.h>
#include <maxscale/query_classifier.hh>
#include <maxscale/protocol/mariadb/mysql.hh>

/* The maximum size for query statements in a transaction (64MB) */
static size_t sql_size_limit = 64 * 1024 * 1024;
/* The size of the buffer for recording latency of individual statements */
static int latency_buf_size = 64 * 1024;
static const int default_sql_size = 4 * 1024;

#define DEFAULT_QUERY_DELIMITER "@@@"
#define DEFAULT_LOG_DELIMITER   ":::"
#define DEFAULT_FILE_NAME       "tpm.log"
#define DEFAULT_NAMED_PIPE      "/tmp/tpmfilter"

class TPM_INSTANCE;
class TPM_SESSION;

namespace
{

namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamString s_filename(
    &s_spec, "filename", "The name of the output file", "tpm.log");

cfg::ParamString s_source(
    &s_spec, "source", "Only include queries done from this address", "");

cfg::ParamString s_user(
    &s_spec, "user", "Only include queries done by this user", "");

cfg::ParamString s_delimiter(
    &s_spec, "delimiter", "Delimiter used to separate the fields", ":::");

cfg::ParamString s_named_pipe(
    &s_spec, "named_pipe", "Only include queries done by this user", "/tmp/tpmfilter");

cfg::ParamString s_query_delimiter(
    &s_spec, "query_delimiter",
    "Delimiter used to distinguish different SQL statements in a transaction",
    "@@@");
}

class Config : public mxs::config::Configuration
{
public:
    Config(const std::string& name, TPM_INSTANCE* instance)
        : mxs::config::Configuration(name, &s_spec)
        , m_instance(instance)
    {
        add_native(&filename, &s_filename);
        add_native(&source, &s_source);
        add_native(&user, &s_user);
        add_native(&delimiter, &s_delimiter);
        add_native(&query_delimiter, &s_query_delimiter);
        add_native(&named_pipe, &s_named_pipe);
    }

    std::string filename;
    std::string source;
    std::string user;
    std::string delimiter;
    std::string query_delimiter;
    std::string named_pipe;

    bool post_configure() override;

private:
    TPM_INSTANCE* m_instance;
};

class TPM_INSTANCE : public mxs::Filter<TPM_INSTANCE, TPM_SESSION>
{
public:
    ~TPM_INSTANCE();
    static TPM_INSTANCE* create(const char* name, mxs::ConfigParameters* params);
    mxs::FilterSession*  newSession(MXS_SESSION* session, SERVICE* service);
    json_t*              diagnostics() const;
    uint64_t             getCapabilities() const;

    mxs::config::Configuration* getConfiguration()
    {
        return &m_config;
    }

    const Config& config() const
    {
        return m_config;
    }

    FILE* fp()
    {
        return m_fp;
    }

    bool enabled() const
    {
        return m_enabled;
    }

    void checkNamedPipe();
    bool post_configure();


private:
    TPM_INSTANCE(const char* name, mxs::ConfigParameters* params)
        : m_config(name, this)
    {
    }


    bool        m_enabled = false;
    FILE*       m_fp = nullptr;
    bool        m_shutdown = false;
    std::thread m_thread;
    Config      m_config;
};

class TPM_SESSION : public mxs::FilterSession
{
public:
    TPM_SESSION(MXS_SESSION* session, SERVICE* service, TPM_INSTANCE* instance)
        : mxs::FilterSession(session, service)
        , m_instance(instance)
        , m_config(instance->config())
    {
    }

    ~TPM_SESSION();
    int32_t routeQuery(GWBUF* pPacket);
    int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

    int            active;
    char*          sql;
    char*          latency;
    struct timeval start;
    char*          current;
    int            n_statements;
    struct timeval total;
    struct timeval current_start;
    struct timeval last_statement_start;
    bool           query_end;
    char*          buf;
    int            sql_index;
    int            latency_index;
    size_t         max_sql_size;

private:
    TPM_INSTANCE* m_instance;
    const Config& m_config;
};

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static const char description[] = "Transaction Performance Monitoring filter";

    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        description,
        "V1.0.1",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &TPM_INSTANCE::s_object,
        NULL,
        NULL,
        NULL,
        NULL,
        {{nullptr}},
        &s_spec
    };

    return &info;
}

bool Config::post_configure()
{
    // check if the file exists first.
    if (access(named_pipe.c_str(), F_OK) == 0)
    {
        // if exists, check if it is a named pipe.
        struct stat st;
        int ret = stat(named_pipe.c_str(), &st);

        // check whether the file is named pipe.
        if (ret == -1 && errno != ENOENT)
        {
            MXS_ERROR("stat() failed on named pipe: %s", strerror(errno));
            return false;
        }
        else if (ret == 0 && S_ISFIFO(st.st_mode))
        {
            // if it is a named pipe, we delete it and recreate it.
            unlink(named_pipe.c_str());
        }
        else
        {
            MXS_ERROR("The file '%s' already exists and it is not a named pipe.", named_pipe.c_str());
            return false;
        }
    }

    // now create the named pipe.
    if (mkfifo(named_pipe.c_str(), 0660) == -1)
    {
        MXS_ERROR("mkfifo() failed on named pipe: %s", strerror(errno));
        return false;
    }

    return m_instance->post_configure();
}

// static
TPM_INSTANCE* TPM_INSTANCE::create(const char* name, mxs::ConfigParameters* params)
{
    return new TPM_INSTANCE(name, params);
}

mxs::FilterSession* TPM_INSTANCE::newSession(MXS_SESSION* session, SERVICE* service)
{
    TPM_INSTANCE* my_instance = this;
    TPM_SESSION* my_session;
    int i;
    const char* remote, * user;

    if ((my_session = new TPM_SESSION(session, service, this)))
    {
        my_session->latency = (char*)MXS_CALLOC(latency_buf_size, sizeof(char));
        my_session->max_sql_size = default_sql_size;    // default max query size of 4k.
        my_session->sql = (char*)MXS_CALLOC(my_session->max_sql_size, sizeof(char));
        memset(my_session->sql, 0x00, my_session->max_sql_size);
        my_session->sql_index = 0;
        my_session->latency_index = 0;
        my_session->n_statements = 0;
        my_session->total.tv_sec = 0;
        my_session->total.tv_usec = 0;
        my_session->current = NULL;
        my_session->active = true;

        if ((!m_config.source.empty() && session->client_remote() != m_config.source)
            || (!m_config.user.empty() && session->user() != m_config.user))
        {
            my_session->active = false;
        }
    }

    return (mxs::FilterSession*)my_session;
}

TPM_SESSION::~TPM_SESSION()
{
    TPM_SESSION* my_session = this;
    TPM_INSTANCE* my_instance = m_instance;

    if (my_instance->fp())
    {
        // flush FP when a session is closed.
        fflush(my_instance->fp());
    }

    MXS_FREE(my_session->sql);
    MXS_FREE(my_session->latency);
}

int32_t TPM_SESSION::routeQuery(GWBUF* queue)
{
    TPM_INSTANCE* my_instance = m_instance;
    TPM_SESSION* my_session = this;
    char* ptr = NULL;
    size_t i;

    if (my_session->active)
    {
        if ((ptr = modutil_get_SQL(queue)) != NULL)
        {
            uint8_t* data = (uint8_t*) GWBUF_DATA(queue);
            uint8_t command = MYSQL_GET_COMMAND(data);

            if (command == MXS_COM_QUERY)
            {
                uint32_t query_type = qc_get_type_mask(queue);
                int query_len = strlen(ptr);
                my_session->query_end = false;

                /* check for commit and rollback */
                if (query_type & QUERY_TYPE_COMMIT)
                {
                    my_session->query_end = true;
                }
                else if (query_type & QUERY_TYPE_ROLLBACK)
                {
                    my_session->query_end = true;
                    my_session->sql_index = 0;
                }

                /* for normal sql statements */
                if (!my_session->query_end)
                {
                    /* check and expand buffer size first. */
                    size_t new_sql_size = my_session->max_sql_size;
                    size_t len = my_session->sql_index + strlen(ptr) + m_config.query_delimiter.size() + 1;

                    /* if the total length of query statements exceeds the maximum limit, print an error and
                     * return */
                    if (len > sql_size_limit)
                    {
                        MXS_WARNING("The size of query statements exceeds the maximum buffer limit of 64MB, "
                                    "the query will be truncated.");
                        len = sql_size_limit;
                    }

                    /* double buffer size until the buffer fits the query */
                    while (len > new_sql_size)
                    {
                        new_sql_size *= 2;
                    }
                    if (new_sql_size > my_session->max_sql_size)
                    {
                        char* new_sql = (char*)MXS_CALLOC(new_sql_size, sizeof(char));
                        memcpy(new_sql, my_session->sql, my_session->sql_index);
                        MXS_FREE(my_session->sql);
                        my_session->sql = new_sql;
                        my_session->max_sql_size = new_sql_size;
                    }

                    /* first statement */
                    if (my_session->sql_index == 0)
                    {
                        memcpy(my_session->sql, ptr, strlen(ptr));
                        my_session->sql_index += strlen(ptr);
                        gettimeofday(&my_session->current_start, NULL);
                    }
                    /* otherwise, append the statement with semicolon as a statement delimiter */
                    else
                    {
                        /* append a query delimiter */
                        memcpy(my_session->sql + my_session->sql_index,
                               m_config.query_delimiter.c_str(),
                               m_config.query_delimiter.size());
                        /* append the next query statement */
                        memcpy(my_session->sql + my_session->sql_index + m_config.query_delimiter.size(),
                               ptr,
                               strlen(ptr));
                        /* set new pointer for the buffer */
                        my_session->sql_index += (m_config.query_delimiter.size() + strlen(ptr));
                    }
                    gettimeofday(&my_session->last_statement_start, NULL);
                }
            }
        }
    }

    MXS_FREE(ptr);
    /* Pass the query downstream */
    return mxs::FilterSession::routeQuery(queue);
}

int32_t TPM_SESSION::clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    TPM_INSTANCE* my_instance = m_instance;
    TPM_SESSION* my_session = this;
    struct      timeval tv, diff;
    int i, inserted;

    /* records latency of the SQL statement. */
    if (my_session->sql_index > 0)
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &(my_session->last_statement_start), &diff);

        /* get latency */
        double millis = (diff.tv_sec * 1000 + diff.tv_usec / 1000.0);

        int written = sprintf(my_session->latency + my_session->latency_index, "%.3f", millis);
        my_session->latency_index += written;
        if (!my_session->query_end)
        {
            written = sprintf(my_session->latency + my_session->latency_index,
                              "%s", m_config.query_delimiter.c_str());
            my_session->latency_index += written;
        }
        if (my_session->latency_index > latency_buf_size)
        {
            MXS_ERROR("Latency buffer overflow.");
        }
    }

    /* found 'commit' and sql statements exist. */
    if (my_session->query_end && my_session->sql_index > 0)
    {
        gettimeofday(&tv, NULL);
        timersub(&tv, &(my_session->current_start), &diff);

        /* get latency */
        uint64_t millis = (diff.tv_sec * (uint64_t)1000 + diff.tv_usec / 1000);
        /* get timestamp */
        uint64_t timestamp = (tv.tv_sec + (tv.tv_usec / (1000 * 1000)));

        *(my_session->sql + my_session->sql_index) = '\0';

        /* print to log. */
        if (my_instance->enabled())
        {
            /* this prints "timestamp | server_name | user_name | latency of entire transaction | latencies of
             * individual statements | sql_statements" */
            fprintf(my_instance->fp(),
                    "%ld%s%s%s%s%s%ld%s%s%s%s\n",
                    timestamp,
                    m_config.delimiter.c_str(),
                    down.front()->target()->name(),
                    m_config.delimiter.c_str(),
                    m_pSession->user().c_str(),
                    m_config.delimiter.c_str(),
                    millis,
                    m_config.delimiter.c_str(),
                    my_session->latency,
                    m_config.delimiter.c_str(),
                    my_session->sql);
        }

        my_session->sql_index = 0;
        my_session->latency_index = 0;
    }

    /* Pass the result upstream */
    return mxs::FilterSession::clientReply(buffer, down, reply);
}

json_t* TPM_INSTANCE::diagnostics() const
{
    return nullptr;
}

uint64_t TPM_INSTANCE::getCapabilities() const
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

TPM_INSTANCE::~TPM_INSTANCE()
{
    mxb_assert(m_thread.joinable());
    m_shutdown = true;
    m_thread.join();

    if (m_fp)
    {
        fclose(m_fp);
    }
}

bool TPM_INSTANCE::post_configure()
{
    m_fp = fopen(m_config.filename.c_str(), "w");

    if (!m_fp)
    {
        MXS_ERROR("Opening output file '%s' for tpmfilter failed due to %d, %s",
                  m_config.filename.c_str(), errno, strerror(errno));
        return false;
    }

    m_thread = std::thread(&TPM_INSTANCE::checkNamedPipe, this);
    return true;
}

void TPM_INSTANCE::checkNamedPipe()
{
    int ret = 0;
    char buffer[2];
    char buf[4096];
    int pipe_fd;

    // open named pipe and this will block until middleware opens it.
    while (!m_shutdown && ((pipe_fd = open(m_config.named_pipe.c_str(), O_RDONLY)) > 0))
    {
        // 1 for start logging, 0 for stopping.
        while (!m_shutdown && ((ret = read(pipe_fd, buffer, 1)) > 0))
        {
            if (buffer[0] == '1')
            {
                // reopens the log file.
                if (m_fp)
                {
                    fclose(m_fp);
                }

                if (!(m_fp = fopen(m_config.filename.c_str(), "w")))
                {
                    MXS_ERROR("Failed to open a log file for tpmfilter.");
                    return;
                }

                m_enabled = true;
            }
            else if (buffer[0] == '0')
            {
                m_enabled = false;
            }
        }
        if (ret == 0)
        {
            close(pipe_fd);
        }
    }

    if (!m_shutdown && (pipe_fd == -1))
    {
        MXS_ERROR("Failed to open the named pipe '%s': %s", m_config.named_pipe.c_str(), strerror(errno));
        return;
    }

    return;
}
