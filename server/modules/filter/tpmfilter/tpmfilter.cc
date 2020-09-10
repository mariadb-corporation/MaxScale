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
#include <maxbase/stopwatch.hh>
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

    void print(const std::string& str)
    {
        fprintf(m_fp, "%s\n", str.c_str());
    }

    void flush()
    {
        if (m_fp)
        {
            fflush(m_fp);
        }
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
    TPM_SESSION(MXS_SESSION* session, SERVICE* service, TPM_INSTANCE* instance);
    ~TPM_SESSION();
    int32_t routeQuery(GWBUF* pPacket);
    int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

private:
    bool                     m_active = true;
    mxb::StopWatch           m_watch;
    mxb::StopWatch           m_trx_watch;
    bool                     m_query_end = false;
    std::vector<std::string> m_sql;
    std::vector<std::string> m_latency;
    TPM_INSTANCE*            m_instance;
    const Config&            m_config;
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
    return new TPM_SESSION(session, service, this);
}

TPM_SESSION::TPM_SESSION(MXS_SESSION* session, SERVICE* service, TPM_INSTANCE* instance)
    : mxs::FilterSession(session, service)
    , m_instance(instance)
    , m_config(instance->config())
{
    if ((!m_config.source.empty() && session->client_remote() != m_config.source)
        || (!m_config.user.empty() && session->user() != m_config.user))
    {
        m_active = false;
    }
}

TPM_SESSION::~TPM_SESSION()
{
    m_instance->flush();
}

int32_t TPM_SESSION::routeQuery(GWBUF* queue)
{
    if (m_active && mxs_mysql_get_command(queue) == MXS_COM_QUERY)
    {
        auto sql = mxs::extract_sql(queue);

        if (!sql.empty())
        {
            auto mask = qc_get_type_mask(queue);

            if (mask & QUERY_TYPE_COMMIT)
            {
                m_query_end = true;
            }
            else if (mask & QUERY_TYPE_ROLLBACK)
            {
                m_query_end = true;
                m_sql.clear();
                m_latency.clear();
            }
            else
            {
                m_query_end = false;
            }

            /* for normal sql statements */
            if (!m_query_end)
            {
                if (sql.empty())
                {
                    m_trx_watch.lap();
                }

                m_sql.push_back(std::move(sql));
                m_watch.lap();
            }
        }
    }

    return mxs::FilterSession::routeQuery(queue);
}

int32_t TPM_SESSION::clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    /* records latency of the SQL statement. */
    if (!m_sql.empty())
    {
        m_latency.push_back(std::to_string(mxb::to_secs(m_watch.lap())));

        /* found 'commit' and sql statements exist. */
        if (m_query_end)
        {
            /* print to log. */
            if (m_instance->enabled())
            {
                const auto& delim = m_config.delimiter;
                std::ostringstream ss;

                /* this prints "timestamp | server_name | user_name | latency of entire transaction |
                 * latencies of individual statements | sql_statements" */
                ss << time(nullptr) << delim
                   << down.front()->target()->name() << delim
                   << m_pSession->user() << delim
                   << mxb::to_secs(m_trx_watch.lap()) * 1000 << delim
                   << mxb::join(m_latency, m_config.query_delimiter) << delim
                   << mxb::join(m_sql, m_config.query_delimiter);

                m_instance->print(ss.str());
            }

            m_sql.clear();
            m_latency.clear();
        }
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
