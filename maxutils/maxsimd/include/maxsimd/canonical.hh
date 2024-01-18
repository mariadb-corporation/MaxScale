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

#include <string>
#include <vector>

namespace maxsimd
{
namespace generic
{
/**
 * Platform-agnostic version of the query canonicalization function
 *
 * Acts as a fallback whenever a specialized implementation is not available. Exposed here to make testing of
 * both specialized and generic implementations easier. Should not be used outside of testing
 *
 * @see maxsimd::get_canonical
 */
std::string* get_canonical(std::string* pSql);

// This is the legacy generic version of the function from 23.08 that was used by non-AVX2 CPUs
std::string* get_canonical_old(std::string* pSql);
}

/**
 * @brief  get_canonical In-place convert sql to canonical form.
 * @param  pSql          Ptr to sql that will be in-place modified.
 * @return pSql          The same pointer that was passed in is returned
 */
std::string* get_canonical(std::string* pSql);
}
