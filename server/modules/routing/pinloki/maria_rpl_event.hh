/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

#pragma once

#include "gtid.hh"

#include <string>
#include <vector>
#include <iosfwd>

#include <mysql.h>
#include <mariadb_rpl.h>

struct st_mariadb_rpl_event;
struct st_mariadb_rpl;

namespace maxsql
{

// NOTE, there is an extra byte at the front of the event buffer
// from mariadb_rpl_fetch (the ok byte). The MariaRplEvent
// will "discard" that byte, so that the raw_data() and
// raw_data_size() match the binlog online documentation.
constexpr int RPL_HEADER_LEN = 19;
constexpr int RPL_SEQ_NR_LEN = 8;
constexpr int RPL_CRC_LEN = 4;

/** Encapsulate a mariadb_rpl event */
class MariaRplEvent
{
public:
    MariaRplEvent(st_mariadb_rpl_event* pEvent, st_mariadb_rpl* handle);

    const st_mariadb_rpl_event& event() const;
    const char*                 raw_data() const;
    size_t                      raw_data_size() const;
    const st_mariadb_rpl& rpl_hndl() const
    {
        return *m_pRpl_handle;
    }

    ~MariaRplEvent();
private:
    st_mariadb_rpl_event* m_pEvent;
    st_mariadb_rpl*       m_pRpl_handle;
};

enum class Verbosity {Name, Some, All};

std::string   dump_rpl_msg(const MariaRplEvent& rpl_msg, Verbosity v);
std::ostream& operator<<(std::ostream& os, const MariaRplEvent& rpl_msg);       // Verbosity::All
}

std::string to_string(mariadb_rpl_event ev);

// TODO: Move this inside MariaRplEvent or RplEvent (maybe combine them?)
std::string get_rotate_name(const char* ptr, size_t len);
