/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include "gtid.hh"
#include "inventory.hh"

namespace pinloki
{

class Config;

struct GtidPosition
{
    GtidPosition(maxsql::Gtid gtid, const std::string& file_name, long file_pos)
        : gtid(gtid)
        , file_name(file_name)
        , file_pos(file_pos)
    {
    }
    maxsql::Gtid gtid;
    std::string  file_name;
    long         file_pos;
};


// Return a vector with GtidPositions of the same size as the input vector.
// The GtidPositions are sorted by file location. If a gtid is not found its
// file_name is empty, and empty positions sort first.
std::vector<GtidPosition> find_gtid_position(std::vector<maxsql::Gtid> gtids,
                                             const Config& cnf);

// Find the last known gtid list. This is used to seed the file rpl_state when the
// writer starts. The function also truncates the latest file if it contains a partial
// transaction or partially written events.
maxsql::GtidList find_last_gtid_list(const Config& cnf);
}
