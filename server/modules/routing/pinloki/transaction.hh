/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "rpl_event.hh"

#include <string>
#include <fstream>

namespace pinloki
{

struct WritePosition
{
    std::string   name;
    std::ofstream file;
    int64_t       write_pos;
};

class Transaction
{
public:
    Transaction() = default;
    Transaction(const maxsql::Gtid& gtid);

    void           add_event(maxsql::RplEvent& rpl_event);
    WritePosition& commit(WritePosition& pos);
};
}
