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
}

bool CsMonitor::command_cluster_start(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput]() {
            cluster_start(sem, ppOutput);
        };

    return command("cluster-start", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_stop(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput]() {
            cluster_stop(sem, ppOutput);
        };

    return command("cluster-stop", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_shutdown(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput]() {
            cluster_shutdown(sem, ppOutput);
        };

    return command("cluster-shutdown", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_add_node(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput]() {
            cluster_add_node(sem, ppOutput);
        };

    return command("cluster-add-node", cmd, sem, ppOutput);
}

bool CsMonitor::command_cluster_remove_node(json_t** ppOutput)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput]() {
            cluster_remove_node(sem, ppOutput);
        };

    return command("cluster-remove-node", cmd, sem, ppOutput);
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

void CsMonitor::cluster_put(const char* zCmd, mxb::Semaphore& sem, json_t** ppOutput)
{
    vector<string> urls;

    for (const MonitorServer* pMserver : servers())
    {
        string url { create_url(*pMserver, m_config.admin_port, zCmd) };

        urls.push_back(url);
    }

    m_http = http::put_async(urls);

    switch (m_http.status())
    {
    case http::Async::PENDING:
        m_pSem = &sem;
        m_ppOutput = ppOutput;
        initiate_delayed_http_check();
        break;

    case http::Async::ERROR:
        PRINT_MXS_JSON_ERROR(ppOutput, "Could not initiate operation '%s' on Columnstore cluster.", zCmd);
        break;

    case http::Async::READY:
        m_pSem = &sem;
        m_ppOutput = ppOutput;
        check_http_result();
    }

    if (!m_pSem)
    {
        sem.post();
    }
}

void CsMonitor::cluster_start(mxb::Semaphore& sem, json_t** ppOutput)
{
    cluster_put("start", sem, ppOutput);
}

void CsMonitor::cluster_stop(mxb::Semaphore& sem, json_t** ppOutput)
{
    cluster_put("stop", sem, ppOutput);
}

void CsMonitor::cluster_shutdown(mxb::Semaphore& sem, json_t** ppOutput)
{
    cluster_put("shutdown", sem, ppOutput);
}

void CsMonitor::cluster_add_node(mxb::Semaphore& sem, json_t** ppOutput)
{
    PRINT_MXS_JSON_ERROR(ppOutput, "cluster-add-node not implemented yet.");
    sem.post();
}

void CsMonitor::cluster_remove_node(mxb::Semaphore& sem, json_t** ppOutput)
{
    PRINT_MXS_JSON_ERROR(ppOutput, "cluster-remove-node not implemented yet.");
    sem.post();
}

void CsMonitor::initiate_delayed_http_check()
{
    mxb_assert(m_dcid == 0);

    long ms = m_http.wait_no_more_than() / 2;

    if (ms == 0)
    {
        ms = 1;
    }

    m_dcid = delayed_call(ms, [this](mxb::Worker::Worker::Call::action_t action) -> bool {
            mxb_assert(m_pSem);
            mxb_assert(m_dcid != 0);

            m_dcid = 0;

            if (action == mxb::Worker::Call::EXECUTE)
            {
                check_http_result();
            }
            else
            {
                // CANCEL
                m_pSem->post();
                m_pSem = nullptr;
            }

            return false;
        });
}

void CsMonitor::check_http_result()
{
    switch (m_http.perform())
    {
    case http::Async::PENDING:
        initiate_delayed_http_check();
        break;

    case http::Async::READY:
        {
            json_t* pOutput = json_object();

            mxb_assert(servers().size() == m_http.results().size());

            auto it = servers().begin();
            auto end = servers().end();
            auto jt = m_http.results().begin();

            while (it != end)
            {
                auto* pMserver = *it;
                const auto& result = *jt;

                json_t* pResult = json_object();

                json_object_set_new(pResult, "code", json_integer(result.code));
                json_object_set_new(pResult, "message", json_string(result.body.c_str()));

                json_object_set_new(pOutput, pMserver->server->name(), pResult);

                ++it;
                ++jt;
            }

            *m_ppOutput = pOutput;
            m_pSem->post();
        }
        break;

    case http::Async::ERROR:
        PRINT_MXS_JSON_ERROR(m_ppOutput, "Fatal HTTP error when contacting Columnstore.");
        m_pSem->post();
        break;
    }
}
