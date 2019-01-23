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

#define MXS_MODULE_NAME "csmon"

#include "csmon.hh"

#include <regex>
#include <vector>
#include <string>

#include <maxscale/modinfo.h>
#include <maxscale/mysql_utils.hh>

namespace
{

constexpr const char* alive_query = "SELECT mcsSystemReady() = 1 && mcsSystemReadOnly() <> 2";
constexpr const char* role_query = "SELECT mcsSystemPrimary()";

// Helper for extracting string results from queries
static std::string do_query(MXS_MONITORED_SERVER* srv, const char* query)
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
        mon_report_query_error(srv);
    }

    return rval;
}

// Returns a numeric version similar to mysql_get_server_version
int get_cs_version(MXS_MONITORED_SERVER* srv)
{
    std::string result = do_query(srv, "SELECT @@version_comment");
    std::regex re("Columnstore ([0-9]*)[.]([0-9]*)[.]([0-9]*)-[0-9]*");
    std::smatch match;
    int rval = 0;

    if (std::regex_match(result, match, re) && match.size() == 4)
    {
        rval = atoi(match[1].str().c_str()) * 10000 + atoi(match[2].str().c_str()) * 100
            + atoi(match[3].str().c_str());
    }

    return rval;
}
}

CsMonitor::CsMonitor(const std::string& name, const std::string& module)
    : MonitorWorkerSimple(name, module)
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

void CsMonitor::update_server_status(MXS_MONITORED_SERVER* srv)
{
    monitor_clear_pending_status(srv, SERVER_MASTER | SERVER_SLAVE | SERVER_RUNNING);
    int status = 0;

    if (do_query(srv, alive_query) == "1")
    {
        status |= SERVER_RUNNING;

        if (get_cs_version(srv) >= 10107)
        {
            // 1.1.7 should support the mcsSystemPrimary function
            // TODO: Update when the actual release is out
            status |= do_query(srv, role_query) == "1" ? SERVER_MASTER : SERVER_SLAVE;
        }
        else
        {
            status |= srv->server == m_primary ? SERVER_MASTER : SERVER_SLAVE;
        }
    }

    monitor_set_pending_status(srv, status);
}

bool CsMonitor::configure(const MXS_CONFIG_PARAMETER* pParams)
{
    m_primary = config_get_server(pParams, "primary");
    return true;
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_MONITOR,
        MXS_MODULE_BETA_RELEASE,
        MXS_MONITOR_VERSION,
        "MariaDB ColumnStore monitor",
        "V1.0.0",
        MXS_NO_MODULE_CAPABILITIES,
        &maxscale::MonitorApi<CsMonitor>::s_api,
        NULL,   /* Process init. */
        NULL,   /* Process finish. */
        NULL,   /* Thread init. */
        NULL,   /* Thread finish. */
        {
            {"primary", MXS_MODULE_PARAM_SERVER},
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
