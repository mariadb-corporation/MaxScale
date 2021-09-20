/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
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
MariaRplEvent::MariaRplEvent(st_mariadb_rpl_event* pEvent, st_mariadb_rpl* handle)
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

const st_mariadb_rpl_event& MariaRplEvent::event() const
{
    return *m_pEvent;
}

const char* MariaRplEvent::raw_data() const
{
    // discard the extra byte in the event buffer
    return reinterpret_cast<const char*>(m_pRpl_handle->buffer) + 1;
}

size_t MariaRplEvent::raw_data_size() const
{
    // discard the extra byte in the event buffer
    return m_pRpl_handle->buffer_size - 1;
}

maxsql::MariaRplEvent::~MariaRplEvent()
{
    if (m_pEvent)
    {
        mariadb_free_rpl_event(m_pEvent);
    }
}
}
