/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maxinfo.hh"

#include <unordered_map>
#include <string>
#include <functional>

#include <maxscale/utils.hh>

#include "../../../core/internal/poll.hh"
#include "../../../core/internal/monitor.hh"
#include "../../../core/internal/monitormanager.hh"
#include "../../../core/internal/server.hh"
#include "../../../core/internal/service.hh"
#include "../../../core/internal/modules.hh"
#include "../../../core/internal/session.hh"

void serviceGetList_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    serviceGetList()->write_as_json(dcb);
}

void serviceGetListenerList_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    serviceGetListenerList()->write_as_json(dcb);
}

void moduleGetList_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    moduleGetList()->write_as_json(dcb);
}

void monitorGetList_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    MonitorManager::monitor_get_list()->write_as_json(dcb);
}

void maxinfoSessionsAll_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    sessionGetList()->write_as_json(dcb);
}

void maxinfoClientSessions_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    sessionGetList()->write_as_json(dcb);
}

void serverGetList_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    Server::getList()->write_as_json(dcb);
}

void eventTimesGetList_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    eventTimesGetList()->write_as_json(dcb);
}

void maxinfo_variables_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    maxinfo_variables()->write_as_json(dcb);
}

void maxinfo_status_http(INFO_INSTANCE* instance, INFO_SESSION* session, DCB* dcb)
{
    maxinfo_status()->write_as_json(dcb);
}

/**
 * Table that maps a URI to a function to call to
 * to obtain the result set related to that URI
 */
static std::unordered_map<std::string, void (*)(INFO_INSTANCE*, INFO_SESSION*, DCB*)> supported_uri
{
    {"/services", serviceGetList_http},
    {"/listeners", serviceGetListenerList_http},
    {"/modules", moduleGetList_http},
    {"/monitors", monitorGetList_http},
    {"/sessions", maxinfoSessionsAll_http},
    {"/clients", maxinfoClientSessions_http},
    {"/servers", serverGetList_http},
    {"/variables", maxinfo_variables_http},
    {"/status", maxinfo_status_http},
    {"/event/times", eventTimesGetList_http}
};

int handle_url(INFO_INSTANCE* instance, INFO_SESSION* session, GWBUF* queue)
{
    std::string uri((char*)GWBUF_DATA(queue));
    auto it = supported_uri.find(uri);

    if (it != supported_uri.end())
    {
        it->second(instance, session, session->dcb);
    }

    return 1;
}
