/*
 * Copyright (c) 2018 MariaDB Corporation Ab
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
#pragma once

#include <maxscale/ccdefs.hh>

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <maxscale/buffer.hh>
#include <maxscale/session.h>
#include <maxscale/resultset.hh>
#include <maxscale/utils.hh>

#include "filter.hh"
#include "service.hh"
#include "session.h"

namespace maxscale
{

typedef struct SESSION_VARIABLE
{
    session_variable_handler_t handler;
    void*                      context;
} SESSION_VARIABLE;

typedef std::unordered_map<std::string, SESSION_VARIABLE> SessionVarsByName;
typedef std::unordered_set<DCB*>                          DCBSet;

// Class that holds the session specific filter data
class SessionFilter
{
public:

    SessionFilter(const SFilterDef& f)
        : filter(f)
        , instance(nullptr)
        , session(nullptr)
    {
    }

    SFilterDef          filter;
    MXS_FILTER*         instance;
    MXS_FILTER_SESSION* session;
};

class Session : public MXS_SESSION
{
public:
    class QueryInfo
    {
    public:
        QueryInfo(const std::shared_ptr<GWBUF>& sQuery);

        json_t* as_json() const;

        bool complete() const
        {
            return m_complete;
        }

        const std::shared_ptr<GWBUF>& query() const
        {
            return m_sQuery;
        }

        void book_server_response(SERVER* pServer, bool final_response);
        void book_as_complete();
        void reset_server_bookkeeping();

        struct ServerInfo
        {
            SERVER*  pServer;
            timespec processed;
        };

    private:
        std::shared_ptr<GWBUF>  m_sQuery;           /*< The packet, COM_QUERY *or* something else. */
        timespec                m_received;         /*< When was it received. */
        timespec                m_completed;        /*< When was it completed. */
        std::vector<ServerInfo> m_server_infos;     /*< When different servers responded. */
        bool                    m_complete = false; /*< Is this information complete? */
    };

    typedef std::deque<QueryInfo> QueryInfos;

    using FilterList = std::vector<SessionFilter>;

    Session(SERVICE* service);
    ~Session();

    bool setup_filters(Service* service);

    const FilterList& get_filters() const
    {
        return m_filters;
    }

    bool  add_variable(const char* name, session_variable_handler_t handler, void* context);
    char* set_variable_value(const char* name_begin,
                             const char* name_end,
                             const char* value_begin,
                             const char* value_end);
    bool remove_variable(const char* name, void** context);
    void retain_statement(GWBUF* pBuffer);
    void dump_statements() const;
    void book_server_response(SERVER* pServer, bool final_response);
    void book_last_as_complete();
    void reset_server_bookkeeping();

    json_t* queries_as_json() const;

    void link_backend_dcb(DCB* dcb)
    {
        mxb_assert(m_dcb_set.count(dcb) == 0);
        m_dcb_set.insert(dcb);
    }

    void unlink_backend_dcb(DCB* dcb)
    {
        mxb_assert(m_dcb_set.count(dcb) == 1);
        m_dcb_set.erase(dcb);
    }

    const DCBSet& dcb_set() const
    {
        return m_dcb_set;
    }

private:
    FilterList        m_filters;
    SessionVarsByName m_variables;
    QueryInfos        m_last_queries;           /*< The N last queries by the client */
    int               m_current_query = -1;     /*< The index of the current query */
    DCBSet            m_dcb_set;                /*< Set of associated backend DCBs */
    uint32_t          m_retain_last_statements; /*< How many statements be retained */
};
}

std::unique_ptr<ResultSet> sessionGetList();
