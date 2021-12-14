/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
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
 * @brief  get_canonical In-place convert sql to canonical form.
 * @param  pSql          Ptr to sql that will be in-place modified.
 * @param  pMarkers      Optimization. Pass in the markers, which can be static
 *                       for the caller, reused for each call to make_markers()
 * @return pSql          The same pointer that was passed in is returned
 */
std::string* get_canonical(std::string* pSql, Markers* pMarkers);
}
