/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#include <array>
#include <string>
#include <tuple>

namespace maxbase
{
/** Pretty string from a size_t, e.g. pretty_size(2000) => "1.95KiB"
 *
 *  @param sz The size to convert
 *
 *  @return The formatted string
 */
std::string pretty_size(size_t sz);
}
