/*
 * Copyright (c) 2021 MariaDB Corporation Ab
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

#pragma once

#include <string>
#include <vector>

namespace maxsimd
{

using Markers = std::vector<const char*>;

/**
 * @brief  is_multi_stmt Determine if sql contains multiple statements.
 * @param  sql           The sql.
 * @param  pMarkers      Optimization. Pass in the markers, which can be static
 *                       for the caller, reused for each call to make_markers()
 * @return bool          true if sql contains multiple statements
 */
bool is_multi_stmt(const std::string& sql, Markers* pMarkers);
}
