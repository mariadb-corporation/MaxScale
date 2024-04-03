/*
 * Copyright (c) 2021 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace maxsimd
{
/**
 * Arguments tells where in the canonical values have been replaced with "?" along with the value that was
 * replaced. By combining the canonical with the arguments you get the original SQL back.
 */
struct CanonicalArgument
{
    uint32_t    pos {0};
    std::string value;

    CanonicalArgument() = default;

    CanonicalArgument(uint32_t pos, std::string value)
        : pos(pos)
        , value(value)
    {
    }

    bool operator==(const CanonicalArgument& other) const
    {
        return pos == other.pos && value == other.value;
    }
};

using CanonicalArgs = std::vector<CanonicalArgument>;

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

// Same as maxsimd::get_canonical_args() except that this uses the fallback implementation. This should only
// be used for testing to verify that the functionality is the same as the specialized implementations.
std::string* get_canonical_args(std::string* pSql, CanonicalArgs* pArgs);

// This is the legacy generic version of the function from 23.08 that was used by non-AVX2 CPUs
std::string* get_canonical_old(std::string* pSql);
}

/**
 * @brief  get_canonical In-place convert sql to canonical form.
 * @param  pSql          Ptr to sql that will be in-place modified.
 * @return pSql          The same pointer that was passed in is returned
 */
std::string* get_canonical(std::string* pSql);

/**
 * Get the canonical version of the query and its arguments
 *
 * This can be used to extract the values of the canonical form.
 *
 * @param pSql  Pointer to the string that contains the raw SQL statement. This is modified in-place.
 * @param pArgs Pointer where the arguments are stored.
 *
 * @return The same pointer that was passed to the function
 */
std::string* get_canonical_args(std::string* pSql, CanonicalArgs* pArgs);

/**
 * Construct the SQL from a canonical query string and its arguments
 *
 * The canonical query for the arguments must be identical to the one where they were created from.
 *
 * @param canonical The canonical query
 * @param args      Arguments for the query
 *
 * @return The recombined SQL query
 */
std::string canonical_args_to_sql(std::string_view canonical, const CanonicalArgs& args);
}
