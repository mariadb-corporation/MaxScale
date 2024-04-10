/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxsql/ccdefs.hh>

#include "gtid.hh"

#include <string>
#include <vector>
#include <iosfwd>

#include <mysql.h>
#include <mariadb_rpl.h>

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
    MariaRplEvent() = default;      // => is_empty() == true
    MariaRplEvent(MARIADB_RPL_EVENT* pEvent, MARIADB_RPL* handle);
    MariaRplEvent(MariaRplEvent&& rhs);
    MariaRplEvent& operator=(MariaRplEvent&& rhs);

    bool is_empty() const
    {
        return m_pRpl_handle == nullptr;
    }
    const MARIADB_RPL_EVENT& event() const;
    const char*              raw_data() const;
    size_t                   raw_data_size() const;

    const MARIADB_RPL& rpl_hndl() const
    {
        return *m_pRpl_handle;
    }

    ~MariaRplEvent();
private:
    size_t raw_data_offset() const;
    MARIADB_RPL_EVENT* m_pEvent = nullptr;
    MARIADB_RPL*       m_pRpl_handle = nullptr;
};
}
