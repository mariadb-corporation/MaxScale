/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "csmonitor.hh"

#include <regex>
#include <vector>
#include <string>
#include <mysql.h>

#include <maxscale/modinfo.hh>
#include <maxscale/mysql_utils.hh>

using std::string;
using std::vector;
using maxscale::MonitorServer;
namespace http = mxb::http;

namespace
{

// TODO: This is just the mockup Columnstore daemon.
const char REST_BASE[] = "/drrtuy/cmapi/0.0.1/node/";

constexpr const char* alive_query = "SELECT mcsSystemReady() = 1 && mcsSystemReadOnly() <> 2";
constexpr const char* role_query = "SELECT mcsSystemPrimary()";

// Helper for extracting string results from queries
static std::string do_query(MonitorServer* srv, const char* query)
{
    std::string rval;
    MYSQL_RES* result;

    if (mxs_mysql_query(srv->con, query) == 0 && (result = mysql_store_result(srv->con)))
    {
        MYSQL_ROW row = mysql_fetch_row(result);

        if (row && row[0])
        {
            rval = row[0];
        }

        mysql_free_result(result);
    }
    else
    {
        srv->mon_report_query_error();
    }

    return rval;
}

// Returns a numeric version similar to mysql_get_server_version
int get_cs_version(MonitorServer* srv)
{
    int rval = -1;
    std::string prefix = "Columnstore ";
    std::string result = do_query(srv, "SELECT @@version_comment");
    auto pos = result.find(prefix);

    auto to_version = [](std::string str) {
            std::istringstream os(str);
            int major = 0, minor = 0, patch = 0;
            char dot;
            os >> major;
            os >> dot;
            os >> minor;
            os >> dot;
            os >> patch;
            return major * 10000 + minor * 100 + patch;
        };

    if (pos != std::string::npos)
    {
        rval = to_version(result.substr(pos + prefix.length()));
    }
    else
    {
        auto cs_version = do_query(srv, "SELECT VARIABLE_VALUE FROM information_schema.GLOBAL_STATUS "
                                        "WHERE VARIABLE_NAME = 'Columnstore_version'");

        if (!cs_version.empty())
        {
            rval = to_version(cs_version);
        }
    }
    return rval;
}

}

class CsMonitor::Command
{
public:
    enum State
    {
        RUNNING,
        READY
    };

    Command(CsMonitor* pMonitor,
            const char* zName,
            vector<string>&& urls,
            mxb::Semaphore* pSem = nullptr,
            json_t** ppOutput = nullptr)
        : m_monitor(*pMonitor)
        , m_name(zName)
        , m_urls(std::move(urls))
        , m_pSem(pSem)
        , m_ppOutput(ppOutput)
    {
    }

    ~Command()
    {
        if (m_dcid)
        {
            m_monitor.cancel_delayed_call(m_dcid);
        }

        if (m_pOutput)
        {
            json_decref(m_pOutput);
        }
    }

    const std::string& name() const
    {
        return m_name;
    }

    State state() const
    {
        return m_state;
    }

    bool is_running() const
    {
        return m_state == RUNNING;
    }

    bool is_ready() const
    {
        return m_state == READY;
    }

    void get_result(json_t** ppOutput)
    {
        mxb_assert(is_ready());

        json_incref(m_pOutput);
        *ppOutput = m_pOutput;
    }

    virtual void init()
    {
        mxb_assert(is_ready());

        m_state = RUNNING;

        switch (m_http.status())
        {
        case http::Async::PENDING:
            order_callback();
            break;

        case http::Async::ERROR:
            PRINT_MXS_JSON_ERROR(&m_pOutput, "Could not initiate operation '%s' on Columnstore cluster.",
                                 m_name.c_str());
            finish();
            break;

        case http::Async::READY:
            check_result();
        }
    }

protected:
    void finish()
    {
        if (m_pOutput && m_ppOutput)
        {
            json_incref(m_pOutput);
            *m_ppOutput = m_pOutput;
        }

        if (m_pSem)
        {
            m_pSem->post();
        }

        m_pSem = nullptr;

        m_state = READY;
    }

    void order_callback()
    {
        mxb_assert(m_dcid == 0);

        long ms = m_http.wait_no_more_than() / 2;

        if (ms == 0)
        {
            ms = 1;
        }

        m_dcid = m_monitor.delayed_call(ms, [this](mxb::Worker::Worker::Call::action_t action) -> bool {
                mxb_assert(m_dcid != 0);

                m_dcid = 0;

                if (action == mxb::Worker::Call::EXECUTE)
                {
                    check_result();
                }
                else
                {
                    // CANCEL
                    finish();
                }

                return false;
            });
    }

    void check_result()
    {
        switch (m_http.perform())
        {
        case http::Async::PENDING:
            order_callback();
            break;

        case http::Async::READY:
            {
                m_pOutput = json_object();

                auto& servers = m_monitor.servers();

                mxb_assert(servers.size() == m_http.results().size());

                auto it = servers.begin();
                auto end = servers.end();
                auto jt = m_http.results().begin();

                while (it != end)
                {
                    auto* pMserver = *it;
                    const auto& result = *jt;

                    json_t* pResult = json_object();

                    json_object_set_new(pResult, "code", json_integer(result.code));
                    json_object_set_new(pResult, "message", json_string(result.body.c_str()));

                    json_object_set_new(m_pOutput, pMserver->server->name(), pResult);

                    ++it;
                    ++jt;
                }

                finish();
            }
            break;

        case http::Async::ERROR:
            PRINT_MXS_JSON_ERROR(&m_pOutput, "Fatal HTTP error when contacting Columnstore.");
            finish();
            break;
        }
    }

protected:
    State           m_state = READY;
    CsMonitor&      m_monitor;
    string          m_name;
    vector<string>  m_urls;
    mxb::Semaphore* m_pSem;
    json_t**        m_ppOutput = nullptr;
    json_t*         m_pOutput = nullptr;
    http::Async     m_http;
    uint32_t        m_dcid = 0;
};

namespace
{

class PutCommand : public CsMonitor::Command
{
public:
    using CsMonitor::Command::Command;

    void init() override final
    {
        m_http = http::put_async(m_urls);

        Command::init();
    }
};

class GetCommand : public CsMonitor::Command
{
public:
    using CsMonitor::Command::Command;

    void init() override final
    {
        m_http = http::get_async(m_urls);

        Command::init();
    }
};

}

CsMonitor::CsMonitor(const std::string& name, const std::string& module)
    : MonitorWorkerSimple(name, module)
    , m_config(name)
{
}

CsMonitor::~CsMonitor()
{
}

// static
CsMonitor* CsMonitor::create(const std::string& name, const std::string& module)
{
    return new CsMonitor(name, module);
}

bool CsMonitor::has_sufficient_permissions()
{
    return test_permissions(alive_query);
}

void CsMonitor::update_server_status(MonitorServer* srv)
{
    srv->clear_pending_status(SERVER_MASTER | SERVER_SLAVE | SERVER_RUNNING);
    int status = 0;

    if (do_query(srv, alive_query) == "1")
    {
        auto version = get_cs_version(srv);

        if (version >= 0)
        {
            status |= SERVER_RUNNING;

            if (version >= 10200)
            {
                // 1.2 supports the mcsSystemPrimary function
                status |= do_query(srv, role_query) == "1" ? SERVER_MASTER : SERVER_SLAVE;
            }
            else
            {
                status |= srv->server == m_config.pPrimary ? SERVER_MASTER : SERVER_SLAVE;
            }
        }
    }

    srv->set_pending_status(status);
}

bool CsMonitor::configure(const mxs::ConfigParameters* pParams)
{
    bool rv = false;

    if (MonitorWorkerSimple::configure(pParams))
    {
        rv = m_config.configure(*pParams);
    }

    return rv;
}

namespace
{

void reject_not_running(const char* zCmd, json_t** ppOutput)
{
    PRINT_MXS_JSON_ERROR(ppOutput,
                         "The Columnstore monitor is not running, cannot "
                         "execute the command '%s'.", zCmd);
}

void reject_call_failed(const char* zCmd, json_t** ppOutput)
{
    PRINT_MXS_JSON_ERROR(ppOutput, "Failed to queue the command '%s' for execution.", zCmd);
}

void reject_command_pending(const char* zCmd, const char* zPending, json_t** ppOutput)
{
    PRINT_MXS_JSON_ERROR(ppOutput,
                         "The command '%s' is running; the command '%s' cannot "
                         "be started until that has finished. Cancel or wait.", zPending, zCmd);
}

}

bool CsMonitor::command_cluster_start(SERVER* pServer, json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        cluster_start(pServer, &sem, ppOutput);
    };

    return command("cluster-start", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_shutdown(SERVER* pServer, json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        cluster_shutdown(pServer, &sem, ppOutput);
    };

    return command("cluster-shutdown", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_ping(SERVER* pServer, json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        cluster_ping(pServer, &sem, ppOutput);
    };

    return command("cluster-ping", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_status(SERVER* pServer, json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        cluster_status(pServer, &sem, ppOutput);
    };

    return command("cluster-status", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_config_get(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput] () {
        cluster_config_get(&sem, ppOutput);
    };

    return command("cluster-config-get", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_config_put(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput] () {
        cluster_config_put(&sem, ppOutput);
    };

    return command("cluster-config-put", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_add_node(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput] () {
        cluster_add_node(&sem, ppOutput);
    };

    return command("cluster-add-node", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_remove_node(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput] () {
        cluster_remove_node(&sem, ppOutput);
    };

    return command("cluster-remove-node", cmd, sem, ppOutput);
}

bool CsMonitor::command_async(const char* zCommand, json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, zCommand, ppOutput] () {
        if (m_sCommand && !m_sCommand->is_ready())
        {
            reject_command_pending(zCommand, m_sCommand->name().c_str(), ppOutput);
        }
        else
        {
            *ppOutput = nullptr;

            string command(zCommand);

            // TODO: Just temporary solution.
            if (command == "cluster-start")
            {
                cluster_start(nullptr);
            }
            else if (command == "cluster-shutdown")
            {
                cluster_shutdown(nullptr);
            }
            else if (command == "cluster-ping")
            {
                cluster_ping(nullptr);
            }
            else if (command == "cluster-status")
            {
                cluster_status(nullptr);
            }
            else if (command == "cluster-config-get")
            {
                cluster_config_get(nullptr);
            }
            else if (command == "cluster-config-put")
            {
                cluster_config_put(nullptr);
            }
            else if (command == "cluster-add-node")
            {
                cluster_add_node();
            }
            else if (command == "cluster-remove-node")
            {
                cluster_remove_node();
            }
            else
            {
                PRINT_MXS_JSON_ERROR(ppOutput, "'%s' is an unknown command.", zCommand);
            }

            if (!*ppOutput)
            {
                string s("Command '");
                s += zCommand;
                s += "' started.";

                *ppOutput = json_string(s.c_str());
            }
        }
        sem.post();
    };

    return command("async", cmd, sem, ppOutput);
}

bool CsMonitor::command_result(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput] () {
        if (!m_sCommand)
        {
            *ppOutput = json_string("No command has been initiated.");
        }
        else if (!m_sCommand->is_ready())
        {
            string s("The command '");
            s += m_sCommand->name();
            s += "' is still running.";

            *ppOutput = json_string(s.c_str());
        }
        else
        {
            m_sCommand->get_result(ppOutput);
        }

        sem.post();
    };

    return command("result", cmd, sem, ppOutput);
}

bool CsMonitor::command_cancel(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput] () {
        if (!m_sCommand)
        {
            *ppOutput = json_string("No command has been initiated.");
        }
        else if (!m_sCommand->is_running())
        {
            string s("The last command '");
            s += m_sCommand->name();
            s += "' is no longer running, cannot be cancelled.";

            *ppOutput = json_string(s.c_str());
        }
        else
        {
            string s("The command '");
            s += m_sCommand->name();
            s += "' was cancelled. Note, current cluster state is unknown.";

            m_sCommand.reset();

            *ppOutput = json_string(s.c_str());
        }

        sem.post();
    };

    return command("result", cmd, sem, ppOutput);
}

bool CsMonitor::command(const char* zCmd, std::function<void()> cmd, mxb::Semaphore& sem, json_t** ppOutput)
{
    bool rv = false;

    if (!is_running())
    {
        reject_not_running(zCmd, ppOutput);
    }
    else
    {
        if (execute(cmd, EXECUTE_QUEUED))
        {
            sem.wait();
            rv = true;
        }
        else
        {
            reject_call_failed(zCmd, ppOutput);
        }
    }

    return rv;
}

namespace
{

string create_url(const MonitorServer& mserver, int64_t port, const char* zOperation)
{
    string url("http://");
    url += mserver.server->address;
    url += ":";
    url += std::to_string(port);
    url += REST_BASE;

    url += zOperation;

    return url;
}

}

void CsMonitor::cluster_get(const char* zCmd, SERVER* pServer, mxb::Semaphore* pSem, json_t** ppOutput)
{
    vector<string> urls;

    for (const MonitorServer* pMserver : servers())
    {
        if (!pServer || (pMserver->server == pServer))
        {
            string url { create_url(*pMserver, m_config.admin_port, zCmd) };

            urls.push_back(url);
        }
    }

    m_sCommand.reset(new GetCommand(this, zCmd, std::move(urls), pSem, ppOutput));
    m_sCommand->init();
}

void CsMonitor::cluster_put(const char* zCmd, SERVER* pServer, mxb::Semaphore* pSem, json_t** ppOutput)
{
    vector<string> urls;

    for (const MonitorServer* pMserver : servers())
    {
        if (!pServer || (pMserver->server == pServer))
        {
            string url { create_url(*pMserver, m_config.admin_port, zCmd) };

            urls.push_back(url);
        }
    }

    m_sCommand.reset(new PutCommand(this, zCmd, std::move(urls), pSem, ppOutput));
    m_sCommand->init();
}

void CsMonitor::cluster_start(SERVER* pServer, mxb::Semaphore* pSem, json_t** ppOutput)
{
    cluster_put("start", pServer, pSem, ppOutput);
}

void CsMonitor::cluster_shutdown(SERVER* pServer, mxb::Semaphore* pSem, json_t** ppOutput)
{
    cluster_put("shutdown", pServer, pSem, ppOutput);
}

void CsMonitor::cluster_ping(SERVER* pServer, mxb::Semaphore* pSem, json_t** ppOutput)
{
    cluster_get("ping", pServer, pSem, ppOutput);
}

void CsMonitor::cluster_status(SERVER* pServer, mxb::Semaphore* pSem, json_t** ppOutput)
{
    cluster_get("status", pServer, pSem, ppOutput);
}

void CsMonitor::cluster_config_get(mxb::Semaphore* pSem, json_t** ppOutput)
{
    cluster_get("config", nullptr, pSem, ppOutput);
}

void CsMonitor::cluster_config_put(mxb::Semaphore* pSem, json_t** ppOutput)
{
    cluster_put("config", nullptr, pSem, ppOutput);
}

void CsMonitor::cluster_add_node(mxb::Semaphore* pSem, json_t** ppOutput)
{
    PRINT_MXS_JSON_ERROR(ppOutput, "cluster-add-node not implemented yet.");
    pSem->post();
}

void CsMonitor::cluster_remove_node(mxb::Semaphore* pSem, json_t** ppOutput)
{
    PRINT_MXS_JSON_ERROR(ppOutput, "cluster-remove-node not implemented yet.");
    pSem->post();
}
