/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "session_command.hh"
#include <maxscale/modutil.h>

void SessionCommand::mark_reply_received()
{
    m_replySent = true;
}

bool SessionCommand::is_reply_received() const
{
    return m_replySent;
}

Buffer SessionCommand::copy_buffer() const
{
    return m_buffer;
}

SessionCommand::SessionCommand(GWBUF *buffer):
    m_buffer(buffer),
    m_replySent(false)
{
}

SessionCommand::~SessionCommand()
{
}

std::string SessionCommand::to_string()
{
    std::string str;
    
    GWBUF **buf = &m_buffer;
    char *sql;
    int sql_len;

    if (modutil_extract_SQL(*buf, &sql, &sql_len))
    {
        str.append(sql, sql_len);
    }

    return str;
}
