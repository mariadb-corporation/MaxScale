/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "maria_rpl_event.hh"
#include "dbconnection.hh"

#include <chrono>
#include <iostream>
#include <iomanip>

#include <zlib.h>

using namespace std::literals::chrono_literals;
using namespace std::literals::string_literals;

namespace maxsql
{
MariaRplEvent::MariaRplEvent(MARIADB_RPL_EVENT* pEvent, MARIADB_RPL* handle)
    : m_pEvent {pEvent}
    , m_pRpl_handle {handle}
{
    // There's a bug in mariadb_rpl in reading the checksum
    // m_pEvent->checksum = *((uint32_t*)(m_pRpl_handle->buffer + m_pRpl_handle->buffer_size - 4));
}

MariaRplEvent::MariaRplEvent(MariaRplEvent&& rhs)
    : m_pEvent(rhs.m_pEvent)
    , m_pRpl_handle(rhs.m_pRpl_handle)
{
    rhs.m_pEvent = nullptr;
    rhs.m_pRpl_handle = nullptr;
}

MariaRplEvent& MariaRplEvent::operator=(MariaRplEvent&& rhs)
{
    m_pEvent = rhs.m_pEvent;
    m_pRpl_handle = rhs.m_pRpl_handle;
    rhs.m_pEvent = nullptr;
    rhs.m_pRpl_handle = nullptr;

    return *this;
}

const MARIADB_RPL_EVENT& MariaRplEvent::event() const
{
    return *m_pEvent;
}
size_t MariaRplEvent::raw_data_offset() const
{
    // Discard the extra byte in the event buffer. If semi-sync is enabled, skip two extra bytes.
    return 1 + (m_pEvent->is_semi_sync ? 2 : 0);
}

const char* MariaRplEvent::raw_data() const
{
    return reinterpret_cast<const char*>(m_pEvent->raw_data) + raw_data_offset();
}

size_t MariaRplEvent::raw_data_size() const
{
    return m_pEvent->raw_data_size - raw_data_offset();
}

maxsql::MariaRplEvent::~MariaRplEvent()
{
    if (m_pEvent)
    {
        mariadb_free_rpl_event(m_pEvent);
    }
}
}
