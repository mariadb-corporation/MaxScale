/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <string_view>

namespace maxsimd
{
/**
 * @brief  is_multi_stmt Determine if sql contains multiple statements.
 * @param  sql           The sql.
 * @return bool          true if sql contains multiple statements
 */
bool is_multi_stmt(std::string_view sql);

namespace generic
{
/**
 * A generic version of maxsimd::is_multi_stmt
 *
 * This is mostly here for testing purposes to verify that the generic implementation produces the same result
 * as the specialized implementations.
 */
bool is_multi_stmt(std::string_view sql);
}
}
