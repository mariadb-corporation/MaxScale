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

#include <maxscale/session.h>
#include <maxscale/resultset.hh>
#include <maxscale/utils.hh>

#include "filter.hh"
#include "service.hh"

namespace maxscale
{
/**
 * Specialization of RegistryTraits for the session registry.
 */
template<>
struct RegistryTraits<MXS_SESSION>
{
    typedef uint64_t id_type;
    typedef MXS_SESSION* entry_type;

    static id_type get_id(entry_type entry)
    {
        return entry->ses_id;
    }
    static entry_type null_entry()
    {
        return NULL;
    }
};

typedef struct SESSION_VARIABLE
{
    session_variable_handler_t handler;
    void*                      context;
} SESSION_VARIABLE;

typedef std::unordered_map<std::string, SESSION_VARIABLE> SessionVarsByName;
typedef std::deque<std::vector<uint8_t>> SessionStmtQueue;
typedef std::unordered_set<DCB*> DCBSet;

// Class that holds the session specific filter data
class SessionFilter
{
public:

    SessionFilter(const SFilterDef& f):
        filter(f),
        instance(nullptr),
        session(nullptr)
    {
    }

    SFilterDef          filter;
    MXS_FILTER*         instance;
    MXS_FILTER_SESSION* session;
};

class Session: public MXS_SESSION
{
public:
    using FilterList = std::vector<SessionFilter>;

    ~Session();

    bool setup_filters(Service* service);

    const FilterList& get_filters() const
    {
        return m_filters;
    }

    bool add_variable(const char* name, session_variable_handler_t handler, void* context);
    char* set_variable_value(const char* name_begin, const char* name_end,
                             const char* value_begin, const char* value_end);
    bool remove_variable(const char* name, void** context);
    void retain_statement(GWBUF* pBuffer);
    void dump_statements() const;

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
    SessionStmtQueue  m_last_statements; /*< The N last statements by the client */
    DCBSet            m_dcb_set;         /*< Set of associated backend DCBs */
};

}

std::unique_ptr<ResultSet> sessionGetList();
