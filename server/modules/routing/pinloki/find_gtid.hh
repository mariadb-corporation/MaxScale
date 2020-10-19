/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
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
    std::string file_name;
    long        file_pos;
};

// Default constructed GtidPosition if not found.
GtidPosition find_gtid_position(const maxsql::Gtid& gtid, const InventoryReader& inv);
}
