/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
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
#include <mysql.h>
#include <mariadb_rpl.h>


namespace maxsql
{
struct Gtid
{
    Gtid() = default;
    Gtid(MARIADB_GTID* mgtid);
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
    uint64_t sequence_nr() const
    {
        return m_sequence_nr;
    }
    uint32_t is_valid() const
    {
        return m_is_valid;
    }

private:
    uint32_t m_domain_id = -1;
    uint32_t m_server_id = -1;
    uint64_t m_sequence_nr = -1;
    bool     m_is_valid = false;
};

inline bool operator==(const Gtid& lhs, const Gtid& rhs)
{
    return lhs.domain_id() == rhs.domain_id()
           && lhs.sequence_nr() == rhs.sequence_nr()
           && lhs.server_id() == rhs.server_id();
}

std::ostream& operator<<(std::ostream& os, const maxsql::Gtid& gtid);

class GtidList
{
public:
    GtidList() = default;
    GtidList(GtidList&&) = default;
    GtidList(const GtidList&) = default;
    GtidList& operator=(GtidList&&) = default;
    GtidList& operator=(const GtidList&) = default;
    GtidList(const std::vector<Gtid>&& gtids);

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

    bool is_empty() const
    {
        return m_gtids.empty();
    }

    /**
     * @brief is_valid
     * @return true if all gtids are valid, including an empty list
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

    /**
     * @brief has_domain Is there a gtid with the given domain in the list
     * @return true If domain is in list
     */
    bool has_domain(uint32_t domain_id) const;

private:
    void sort();

    std::vector<Gtid> m_gtids;
    bool              m_is_valid = false;
};

std::ostream& operator<<(std::ostream& os, const GtidList& lst);
}
