/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxbase/ccdefs.hh>
#include <maxsql/ccdefs.hh>
#include <sstream>
#include <string>
#include <vector>
#include <tuple>

struct st_mariadb_gtid;

namespace maxsql
{
struct Gtid
{
    Gtid() = default;
    Gtid(st_mariadb_gtid* mgtid);
    Gtid(uint32_t domain, uint32_t server_id, uint64_t sequence)
        : m_domain_id(domain)
        , m_server_id(server_id)
        , m_sequence_nr(sequence)
        , m_is_valid(true)
    {
    }
    Gtid(const std::tuple<uint32_t, uint32_t, uint64_t>& t)
        : Gtid(std::get<0>(t), std::get<1>(t), std::get<2>(t))
    {
    }

    std::string to_string() const;
    static Gtid from_string(const std::string& cstr);

    uint32_t domain_id() const
    {
        return m_domain_id;
    }
    uint32_t server_id() const
    {
        return m_server_id;
    }
    uint32_t sequence_nr() const
    {
        return m_sequence_nr;
    }
    uint32_t is_valid() const
    {
        return m_is_valid;
    }

    /** Decrement the sequence number. If it would become 0,
     *  return an invalid Gtid.
     */
    Gtid previous() const;

private:
    uint32_t m_domain_id = -1;
    uint32_t m_server_id = -1;
    uint64_t m_sequence_nr = -1;
    bool     m_is_valid = false;
};

std::ostream& operator<<(std::ostream& os, const maxsql::Gtid& gtid);

// Sorting really only needs to be done by domain_id, as long as it is only GtidList doing it
// inline bool operator<(const Gtid& lhs, const Gtid& rhs)
// {
//    return lhs.domain_id() < rhs.domain_id()
//           || (lhs.domain_id() == rhs.domain_id() && lhs.sequence_nr() < rhs.sequence_nr());
// }

// inline bool operator==(const Gtid& lhs, const Gtid& rhs)
// {
//    return lhs.domain_id() == rhs.domain_id()
//           && lhs.server_id() == rhs.server_id()
//           && lhs.sequence_nr() == rhs.sequence_nr();
// }

class GtidList
{
public:
    GtidList() = default;
    GtidList(GtidList&&) = default;
    GtidList(const GtidList&) = default;
    GtidList& operator=(GtidList&&) = default;
    GtidList& operator=(const GtidList&) = default;
    GtidList(const std::vector<Gtid>&& gtids);

    void clear();
    void replace(const Gtid& gtid);

    std::string     to_string() const;
    static GtidList from_string(const std::string& cstr);

    /**
     * @brief gtids
     * @return gtids sorted by domain
     */
    const std::vector<Gtid>& gtids() const
    {
        return m_gtids;
    }

    /**
     * @brief is_valid
     * @return true if all gtids are valid
     */
    bool is_valid() const
    {
        return m_is_valid;
    }

    /**
     * Is the given GTID behind this GTID
     *
     * @param other The other GTID to compare to
     *
     * @return True if all GTID domains in `other` are present and all sequences in
     *         those domains compare less than or equal.
     */
    bool is_included(const GtidList& other) const;

private:
    void sort();

    std::vector<Gtid> m_gtids;
    bool              m_is_valid = false;
};

std::ostream& operator<<(std::ostream& os, const GtidList& lst);
}
