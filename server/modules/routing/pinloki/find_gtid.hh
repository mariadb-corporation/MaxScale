/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-11
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

struct GtidPosition
{
    maxsql::Gtid gtid;
    std::string  file_name;
    long         file_pos;
};

// Return a vector with GtidPositions of the same size as the input vector.
// The GtidPositions are sorted by file location. If a gtid is not found its
// file_name is empty, and empty positions sort first.
std::vector<GtidPosition> find_gtid_position(const std::vector<maxsql::Gtid>& gtid_list,
                                             const InventoryReader& inv);
}
