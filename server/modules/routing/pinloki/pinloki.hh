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

#include <string>
#include <array>
#include <maxbase/exception.hh>

namespace pinloki
{
DEFINE_EXCEPTION(BinlogReadError);

static std::array<char, 4> PINLOKI_MAGIC = {char(0xfe), 0x62, 0x69, 0x6e};

struct FileLocation
{
    std::string file_name;
    long        loc;
};
}
