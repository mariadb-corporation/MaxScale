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
#include <sstream>
#include <mysql.h>

#include <maxscale/modinfo.hh>
#include <maxscale/mysql_utils.hh>
#include "csrest.hh"

using std::string;
using std::stringstream;
using std::vector;
using maxscale::MonitorServer;
namespace http = mxb::http;

namespace
{

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

json_t* create_response(const vector<CsMonitorServer*>& mservers, const vector<http::Result>& results)
{
    mxb_assert(mservers.size() == results.size());

    json_t* pResponse = json_object();

    auto it = mservers.begin();
    auto end = mservers.end();
    auto jt = results.begin();

    while (it != end)
    {
        auto* pMserver = *it;
        const auto& result = *jt;

        json_t* pResult = json_object();

        json_object_set_new(pResult, "code", json_integer(result.code));
        json_object_set_new(pResult, "message", json_string(result.body.c_str()));

        json_object_set_new(pResponse, pMserver->server->name(), pResult);

        ++it;
        ++jt;
    }

    return pResponse;
}

vector<http::Result>::const_iterator find_first_failed(const vector<http::Result>& results)
{
    return std::find_if(results.begin(), results.end(), [](const http::Result& result) -> bool {
            return result.code != 200;
        });
}

vector<http::Result>::iterator find_first_failed(vector<http::Result>& results)
{
    return std::find_if(results.begin(), results.end(), [](const http::Result& result) -> bool {
            return result.code != 200;
        });
}

}

class CsMonitor::Command
{
public:
    enum State
    {
        IDLE,
        RUNNING,
        READY
    };

    Command(CsMonitor* pMonitor,
            cs::rest::Action action,
            vector<string>&& urls,
            const http::Config& config,
            mxb::Semaphore* pSem = nullptr,
            json_t** ppOutput = nullptr)
        : m_monitor(*pMonitor)
        , m_name(cs::rest::to_string(action))
        , m_config(config)
        , m_urls(std::move(urls))
        , m_pSem(pSem)
        , m_ppOutput(ppOutput)
    {
    }

    Command(CsMonitor* pMonitor,
            cs::rest::Action action,
            vector<string>&& urls,
            string&& body,
            const http::Config& config,
            mxb::Semaphore* pSem = nullptr,
            json_t** ppOutput = nullptr)
        : m_monitor(*pMonitor)
        , m_name(cs::rest::to_string(action))
        , m_config(config)
        , m_urls(std::move(urls))
        , m_body(std::move(body))
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

    bool is_idle() const
    {
        return m_state == IDLE;
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

        m_state = IDLE;
    }

    virtual void init()
    {
        mxb_assert(is_idle());

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
                m_pOutput = create_response(m_monitor.servers(), m_http.results());

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
    State           m_state = IDLE;
    CsMonitor&      m_monitor;
    string          m_name;
    http::Config    m_config;
    vector<string>  m_urls;
    string          m_body;
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
        m_http = http::put_async(m_urls, m_config);

        Command::init();
    }
};

class GetCommand : public CsMonitor::Command
{
public:
    using CsMonitor::Command::Command;

    void init() override final
    {
        m_http = http::get_async(m_urls, m_config);

        Command::init();
    }
};

}

CsMonitor::CsMonitor(const std::string& name, const std::string& module)
    : MonitorWorkerSimple(name, module)
    , m_config(name)
{
    // The CS daemon uses a self-signed certificate.
    m_http_config.ssl_verifypeer = false;
    m_http_config.ssl_verifyhost = false;
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

        if (rv)
        {
            m_http_config.headers["X-API-KEY"] = m_config.api_key;
        }
    }

    return rv;
}

namespace
{

void reject_not_running(json_t** ppOutput, const char* zCmd)
{
    PRINT_MXS_JSON_ERROR(ppOutput,
                         "The Columnstore monitor is not running, cannot "
                         "execute the command '%s'.", zCmd);
}

void reject_call_failed(json_t** ppOutput, const char* zCmd)
{
    PRINT_MXS_JSON_ERROR(ppOutput, "Failed to queue the command '%s' for execution.", zCmd);
}

void reject_command_pending(json_t** ppOutput, const char* zPending)
{
    PRINT_MXS_JSON_ERROR(ppOutput,
                         "The command '%s' is running; another command cannot "
                         "be started until that has finished. Cancel or wait.", zPending);
}

}

bool CsMonitor::command_cluster_start(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cluster_start(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "cluster-start", cmd);
}

bool CsMonitor::command_cluster_shutdown(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cluster_shutdown(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "cluster-shutdown", cmd);
}

bool CsMonitor::command_cluster_ping(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cluster_ping(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "cluster-ping", cmd);
}

bool CsMonitor::command_cluster_status(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cluster_status(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "cluster-status", cmd);
}

bool CsMonitor::command_cluster_config_get(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, pServer, ppOutput] () {
        if (ready_to_run(ppOutput))
        {
            cluster_config_get(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "cluster-config-get", cmd);
}

bool CsMonitor::command_cluster_config_set(json_t** ppOutput, const char* zJson, CsMonitorServer* pServer)
{
    bool rv = false;

    auto len = strlen(zJson);
    if (is_valid_json(ppOutput, zJson, len))
    {
        mxb::Semaphore sem;
        string body(zJson, zJson + len);

        auto cmd = [this, ppOutput, &sem, &body, pServer] () {
            if (ready_to_run(ppOutput))
            {
                cluster_config_set(ppOutput, &sem, std::move(body), pServer);
            }
            else
            {
                sem.post();
            }
        };

        rv = command(ppOutput, sem, "cluster-config-put", cmd);
    }

    return rv;
}

bool CsMonitor::command_cluster_mode_set(json_t** ppOutput, const char* zMode)
{
    bool rv = false;
    Mode mode;

    if (from_string(zMode, &mode))
    {
        mxb::Semaphore sem;

        auto cmd = [this, ppOutput, &sem, mode] () {
            if (ready_to_run(ppOutput))
            {
                // TODO: This is actually only to be sent to the master.
                cluster_mode_set(ppOutput, &sem, mode);
            }
            else
            {
                sem.post();
            }
        };

        rv = command(ppOutput, sem, "cluster-mode-set", cmd);
    }

    return rv;
}

bool CsMonitor::command_cluster_add_node(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput, pServer] () {
        if (ready_to_run(ppOutput))
        {
            cluster_add_node(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "cluster-add-node", cmd);
}

bool CsMonitor::command_cluster_remove_node(json_t** ppOutput, CsMonitorServer* pServer)
{
    mxb::Semaphore sem;

    auto cmd = [this, &sem, ppOutput, pServer] () {
        if (ready_to_run(ppOutput))
        {
            cluster_remove_node(ppOutput, &sem, pServer);
        }
        else
        {
            sem.post();
        }
    };

    return command(ppOutput, sem, "cluster-remove-node", cmd);
}

//static
bool CsMonitor::from_string(const char* zMode, Mode* pMode)
{
    bool rv = true;

    if (strcmp("read-only", zMode) == 0)
    {
        *pMode = READ_ONLY;
    }
    else if (strcmp("read-write", zMode) == 0)
    {
        *pMode = READ_WRITE;
    }
    else
    {
        rv = false;
    }

    return rv;
}

//static
const char* CsMonitor::to_string(Mode mode)
{
    switch (mode)
    {
    case READ_ONLY:
        return "read-only";

    case READ_WRITE:
        return "read-write";

    default:
        mxb_assert(!true);
        return "";
    }
}

bool CsMonitor::ready_to_run(json_t** ppOutput) const
{
    bool rv = true;

    if (m_sCommand)
    {
        switch (m_sCommand->state())
        {
        case Command::IDLE:
            break;

        case Command::READY:
            PRINT_MXS_JSON_ERROR(ppOutput,
                                 "The command '%s' is ready; its result must be fetched before "
                                 "another command can be issued.",
                                 m_sCommand->name().c_str());
            rv = false;
            break;

        case Command::RUNNING:
            PRINT_MXS_JSON_ERROR(ppOutput,
                                 "The command '%s' is running; another command cannot "
                                 "be started until that has finished. Cancel or wait.",
                                 m_sCommand->name().c_str());
            rv = false;
            break;
        }
    }

    return rv;
}

//static
bool CsMonitor::is_valid_json(json_t** ppOutput, const char* zJson, size_t len)
{
    bool rv = false;

    json_error_t error;
    json_t* pJson = json_loadb(zJson, len, 0, &error);

    if (pJson)
    {
        json_decref(pJson);
        rv = true;
    }
    else
    {
        *ppOutput = mxs_json_error_append(nullptr, "Provided string '%s' is not valid JSON: %s",
                                          zJson, error.text);

    }

    return rv;
}

bool CsMonitor::command(json_t** ppOutput, mxb::Semaphore& sem, const char* zCmd, std::function<void()> cmd)
{
    bool rv = false;

    if (!is_running())
    {
        reject_not_running(ppOutput, zCmd);
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
            reject_call_failed(ppOutput, zCmd);
        }
    }

    return rv;
}

void CsMonitor::cluster_get(json_t** ppOutput,
                            mxb::Semaphore* pSem,
                            cs::rest::Action action,
                            CsMonitorServer* pServer)
{
    vector<string> urls;

    if (pServer)
    {
        urls.push_back(cs::rest::create_url(*pServer, m_config.admin_port, action));
    }
    else
    {
        for (auto* pS : servers())
        {
            urls.push_back(cs::rest::create_url(*pS, m_config.admin_port, action));
        }
    }

    m_sCommand.reset(new GetCommand(this, action, std::move(urls), m_http_config, pSem, ppOutput));
    m_sCommand->init();
}

void CsMonitor::cluster_put(json_t** ppOutput,
                            mxb::Semaphore* pSem,
                            cs::rest::Action action,
                            CsMonitorServer* pServer,
                            string&& body)
{
    mxb_assert(!m_sCommand || m_sCommand->is_idle());

    vector<string> urls;

    if (pServer)
    {
        urls.push_back(cs::rest::create_url(*pServer, m_config.admin_port, action));
    }
    else
    {
        for (auto* pS : servers())
        {
            urls.push_back(cs::rest::create_url(*pServer, m_config.admin_port, action));
        }
    }

    m_sCommand.reset(new PutCommand(this, action, std::move(urls),
                                    std::move(body), m_http_config, pSem, ppOutput));
    m_sCommand->init();
}

void CsMonitor::cluster_start(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    cluster_put(ppOutput, pSem, cs::rest::START, pServer);
}

void CsMonitor::cluster_shutdown(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    cluster_put(ppOutput, pSem, cs::rest::SHUTDOWN, pServer);
}

void CsMonitor::cluster_ping(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    cluster_get(ppOutput, pSem, cs::rest::PING, pServer);
}

void CsMonitor::cluster_status(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    cluster_get(ppOutput, pSem, cs::rest::STATUS, pServer);
}

void CsMonitor::cluster_config_get(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    cluster_get(ppOutput, pSem, cs::rest::CONFIG, pServer);
}

void CsMonitor::cluster_config_set(json_t** ppOutput, mxb::Semaphore* pSem,
                                   string&& body, CsMonitorServer* pServer)
{
    cluster_put(ppOutput, pSem, cs::rest::CONFIG, pServer, std::move(body));
}

void CsMonitor::cluster_mode_set(json_t** ppOutput, mxb::Semaphore* pSem, Mode mode)
{
    stringstream ss;
    ss << "{ \"mode\":" << "\"" << to_string(mode) << "\" }";
    string body = ss.str();

    cluster_put(ppOutput, pSem, cs::rest::CONFIG, nullptr, std::move(body));
}

void CsMonitor::cluster_add_node(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    /*
      cluster add node { IP | DNS }
      - Sends GET /node/ping to the new node.
        Fail the command  if the previous call failed.
      - Sends GET /node/config to { all | only one } of the listed nodes.
        Uses the config/-s to produce new versions of the configs.
      - Sends PUT /node/config to the old nodes and to the new node.

      The previous action forces the config reload for all services.
    */

    http::Result result = http::get(cs::rest::create_url(*pServer, m_config.admin_port, cs::rest::PING));

    if (result.code == 200)
    {
        vector<const CsMonitorServer*> mservers;
        vector<string> urls;

        for (const auto* pS : this->servers())
        {
            if (pS != pServer)
            {
                mservers.push_back(pS);
                urls.push_back(cs::rest::create_url(*pS, m_config.admin_port, cs::rest::CONFIG));
            }
        }

        if (urls.empty())
        {
            // TODO: First node requires additional handling.
            PRINT_MXS_JSON_ERROR(ppOutput,
                                 "There are no other nodes in the cluster and thus "
                                 "no-one from which to ask the configuration. Server '%s' "
                                 "cannot be added to the cluster, but must be configured "
                                 "explicitly.", pServer->name());
        }
        else
        {
            vector<http::Result> results = http::get(urls);

            auto it = find_first_failed(results);

            if (it != results.end())
            {
                PRINT_MXS_JSON_ERROR(ppOutput, "Could not get config from server '%s', new node cannot "
                                     "be added: %s",
                                     mservers[it - results.begin()]->server->name(), it->body.c_str());
            }
            else
            {
                auto it = std::adjacent_find(results.begin(), results.end(),
                                             [](const auto& l, const auto& r) {
                                                 return l.body != r.body;
                                             });

                if (it != results.end())
                {
                    PRINT_MXS_JSON_ERROR(ppOutput, "Configuration of all nodes is not identical. Not "
                                         "possible to add new node.");
                }
                else
                {
                    // TODO: Update configuration to INCLUDE the new node.
                    // TODO: Change body to string.
                    vector<char> body(results.begin()->body.data(),
                                      results.begin()->body.data() + results.begin()->body.length());

                    vector<string> urls;
                    for (const auto* pS : servers())
                    {
                        urls.push_back(cs::rest::create_url(*pS, m_config.admin_port, cs::rest::CONFIG));
                    }

                    vector<http::Result> results = http::put(urls, body);

                    auto it = find_first_failed(results);

                    if (it != results.end())
                    {
                        PRINT_MXS_JSON_ERROR(ppOutput,
                                             "Could not update configuration of all nodes. Cluster state "
                                             "is now indeterminate.");
                    }
                    else
                    {
                        *ppOutput = create_response(servers(), results);
                    }
                }
            }
        }
    }
    else
    {
        PRINT_MXS_JSON_ERROR(ppOutput, "Could not ping server '%s': %s",
                             pServer->name(), result.body.c_str());
    }

    pSem->post();
}

void CsMonitor::cluster_remove_node(json_t** ppOutput, mxb::Semaphore* pSem, CsMonitorServer* pServer)
{
    /*
      cluster remove node { nodeid | IP | DNS }  { force }
      - Sends GET /node/ping to the node to be removed
      - If force isn’t set then run cluster mode set read-only first.
        Don’t send this to the target node if ping fail
      - Sends  PUT /node/shutdown to the removed node with JSON parameters
        (immediate shutdown) command if the ping call returns.
      - Sends GET /node/config to { all | only one } of the listed nodes.
      Uses the config/-s to produce new versions of the configs.
      - Sends PUT /node/config to the old nodes and to the new node.
      The previous action forces the restart of the services.

      Currently no force and no read-only mode.
    */

    *ppOutput = nullptr;

    http::Result ping = http::get(cs::rest::create_url(*pServer, m_config.admin_port, cs::rest::PING));

    if (ping.code == 200)
    {
        http::Result shutdown = http::get(cs::rest::create_url(*pServer, m_config.admin_port,
                                                               cs::rest::SHUTDOWN));

        if (shutdown.code != 200)
        {
            // TODO: Perhaps appropriate to ignore error?
            PRINT_MXS_JSON_ERROR(ppOutput, "Could not shutdown '%s'. Cannot remove the node: %s",
                                 pServer->name(), shutdown.body.c_str());
        }
    }

    if (!*ppOutput)
    {
        vector<const MonitorServer*> mservers;
        vector<string> urls;

        for (const auto* pS : this->servers())
        {
            if (pS != pServer)
            {
                mservers.push_back(pS);
                urls.push_back(cs::rest::create_url(*pS, m_config.admin_port, cs::rest::CONFIG));
            }
        }

        // TODO Can you remove the last node?
        if (!urls.empty())
        {
            vector<http::Result> results = http::get(urls);

            auto it = find_first_failed(results);

            if (it != results.end())
            {
                PRINT_MXS_JSON_ERROR(ppOutput, "Could not get config from server '%s', node cannot "
                                     "be removed: %s",
                                     mservers[it - results.begin()]->server->name(), it->body.c_str());
            }
            else
            {
                auto it = std::adjacent_find(results.begin(), results.end(), [](const auto& l, const auto& r) {
                        return l.body != r.body;
                    });

                if (it != results.end())
                {
                    PRINT_MXS_JSON_ERROR(ppOutput, "Configuration of all nodes is not identical. Not "
                                         "possible to remove a node.");
                }
                else
                {
                    // TODO: Update configuration to EXCLUDE the removed node.
                    // TODO: Change body to string.
                    vector<char> body(results.begin()->body.data(),
                                      results.begin()->body.data() + results.begin()->body.length());

                    vector<string> urls;
                    for (const auto* pS : servers())
                    {
                        urls.push_back(cs::rest::create_url(*pS, m_config.admin_port, cs::rest::CONFIG));
                    }

                    vector<http::Result> results = http::put(urls, body);

                    auto it = find_first_failed(results);

                    if (it != results.end())
                    {
                        PRINT_MXS_JSON_ERROR(ppOutput, "Could not update configuration of all nodes. "
                                             "Cluster state is now indeterminate.");
                    }
                    else
                    {
                        *ppOutput = create_response(servers(), results);
                    }
                }
            }
        }
    }

    pSem->post();
}

CsMonitorServer* CsMonitor::create_server(SERVER* pServer,
                                          const mxs::MonitorServer::SharedSettings& shared)
{
    return new CsMonitorServer(pServer, shared, m_config.admin_port);
}
