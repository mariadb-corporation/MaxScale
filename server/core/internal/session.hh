#pragma once
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

#include <maxscale/cppdefs.hh>

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <maxscale/session.h>
#include <maxscale/resultset.hh>
#include <maxscale/utils.hh>

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

}

typedef struct SESSION_VARIABLE
{
    session_variable_handler_t handler;
    void*                      context;
} SESSION_VARIABLE;

typedef std::unordered_map<std::string, SESSION_VARIABLE> SessionVarsByName;
typedef std::deque<std::vector<uint8_t>> SessionStmtQueue;
typedef std::unordered_set<DCB*> DCBSet;

class Session: public MXS_SESSION
{
private:
    SessionVarsByName m_variables;
    SessionStmtQueue  m_last_statements; /*< The N last statements by the client */
    DCBSet            m_dcb_set;         /*< Set of associated backend DCBs */
};

std::unique_ptr<ResultSet> sessionGetList();
