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

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <thread>
#include <fstream>

#include <maxbase/stopwatch.hh>
#include <maxscale/filter.hh>
#include <maxscale/modutil.hh>
#include <maxscale/query_classifier.hh>

class TpmFilter;
class TpmSession;

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
    Config(const std::string& name, TpmFilter* instance)
        : mxs::config::Configuration(name, &s_spec)
        , m_instance(instance)
    {
        add_native(&Config::filename, &s_filename);
        add_native(&Config::source, &s_source);
        add_native(&Config::user, &s_user);
        add_native(&Config::delimiter, &s_delimiter);
        add_native(&Config::query_delimiter, &s_query_delimiter);
        add_native(&Config::named_pipe, &s_named_pipe);
    }

    std::string filename;
    std::string source;
    std::string user;
    std::string delimiter;
    std::string query_delimiter;
    std::string named_pipe;

    bool post_configure() override;

private:
    TpmFilter* m_instance;
};

class TpmFilter : public mxs::Filter<TpmFilter, TpmSession>
{
public:
    ~TpmFilter();
    static TpmFilter*   create(const char* name, mxs::ConfigParameters* params);
    mxs::FilterSession* newSession(MXS_SESSION* session, SERVICE* service);
    json_t*             diagnostics() const;
    uint64_t            getCapabilities() const;

    mxs::config::Configuration* getConfiguration()
    {
        return &m_config;
    }

    const Config& config() const
    {
        return m_config;
    }

    void flush()
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_file.flush();
    }

    void write(const std::string& str)
    {
        std::lock_guard<std::mutex> guard(m_lock);
        m_file << str;
    }

    bool enabled() const
    {
        return m_enabled;
    }

    void check_named_pipe();
    bool post_configure();


private:
    TpmFilter(const char* name, mxs::ConfigParameters* params)
        : m_config(name, this)
    {
    }

    std::mutex    m_lock;
    bool          m_enabled = false;
    std::ofstream m_file;
    bool          m_shutdown = false;
    std::thread   m_thread;
    Config        m_config;
};

class TpmSession : public mxs::FilterSession
{
public:
    TpmSession(MXS_SESSION* session, SERVICE* service, TpmFilter* instance);
    ~TpmSession();
    int32_t routeQuery(GWBUF* pPacket);
    int32_t clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply);

private:
    bool                     m_active = true;
    mxb::StopWatch           m_watch;
    mxb::StopWatch           m_trx_watch;
    bool                     m_query_end = false;
    std::vector<std::string> m_sql;
    std::vector<std::string> m_latency;
    TpmFilter*               m_instance;
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
        &TpmFilter::s_object,
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
TpmFilter* TpmFilter::create(const char* name, mxs::ConfigParameters* params)
{
    return new TpmFilter(name, params);
}

mxs::FilterSession* TpmFilter::newSession(MXS_SESSION* session, SERVICE* service)
{
    return new TpmSession(session, service, this);
}

TpmSession::TpmSession(MXS_SESSION* session, SERVICE* service, TpmFilter* instance)
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

TpmSession::~TpmSession()
{
    m_instance->flush();
}

int32_t TpmSession::routeQuery(GWBUF* queue)
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
            if (!m_query_end && m_pSession->is_trx_active())
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

int32_t TpmSession::clientReply(GWBUF* buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply)
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

                /* this prints "timestamp | server_name | user_name | latency of entire transaction |
                 * latencies of individual statements | sql_statements" */
                std::ostringstream ss;
                ss << time(nullptr) << delim
                   << down.front()->target()->name() << delim
                   << m_pSession->user() << delim
                   << mxb::to_secs(m_trx_watch.lap()) * 1000 << delim
                   << mxb::join(m_latency, m_config.query_delimiter) << delim
                   << mxb::join(m_sql, m_config.query_delimiter);
                m_instance->write(ss.str());
            }

            m_sql.clear();
            m_latency.clear();
        }
    }

    /* Pass the result upstream */
    return mxs::FilterSession::clientReply(buffer, down, reply);
}

json_t* TpmFilter::diagnostics() const
{
    return nullptr;
}

uint64_t TpmFilter::getCapabilities() const
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

TpmFilter::~TpmFilter()
{
    mxb_assert(m_thread.joinable());
    m_shutdown = true;
    m_thread.join();
}

bool TpmFilter::post_configure()
{
    std::lock_guard<std::mutex> guard(m_lock);
    m_file.open(m_config.filename);

    if (!m_file)
    {
        MXS_ERROR("Opening output file '%s' for tpmfilter failed due to %d, %s",
                  m_config.filename.c_str(), errno, strerror(errno));
        return false;
    }

    m_thread = std::thread(&TpmFilter::check_named_pipe, this);
    return true;
}

void TpmFilter::check_named_pipe()
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
                std::lock_guard<std::mutex> guard(m_lock);
                m_file.open(m_config.filename);

                if (!m_file)
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
