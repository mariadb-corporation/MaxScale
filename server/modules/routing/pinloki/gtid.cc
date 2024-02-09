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

#define BOOST_ERROR_CODE_HEADER_ONLY 1

#include "gtid.hh"
#include <maxbase/string.hh>
#include <maxscale/log.hh>
#include <maxbase/log.hh>
#include <maxscale/boost_spirit_utils.hh>
#include <algorithm>
#include <sstream>
#include <mysql.h>
#include <mariadb_rpl.h>
#include <iostream>
#include <iomanip>

namespace maxsql
{

Gtid::Gtid(MARIADB_GTID* mgtid)
    : m_domain_id(mgtid->domain_id)
    , m_server_id(mgtid->server_id)
    , m_sequence_nr(mgtid->sequence_nr)
    , m_is_valid(true)
{
}

std::string Gtid::to_string() const
{
    return MAKE_STR(m_domain_id << '-' << m_server_id << '-' << m_sequence_nr);
}

Gtid Gtid::from_string(const std::string& gtid_str)
{
    if (gtid_str.empty())
    {
        return Gtid();
    }

    namespace x3 = boost::spirit::x3;

    const auto gtid_parser = x3::uint32 >> '-' >> x3::uint32 >> '-' >> x3::uint64;

    std::tuple<uint32_t, uint32_t, uint64_t> result;    // intermediary to avoid boost-fusionizing Gtid.

    auto first = begin(gtid_str);
    auto success = parse(first, end(gtid_str), gtid_parser, result);

    if (success && first == end(gtid_str))
    {
        return Gtid(result);
    }
    else
    {
        MXB_SERROR("Invalid gtid string: '" << gtid_str);
        return Gtid();
    }
}

std::ostream& operator<<(std::ostream& os, const Gtid& gtid)
{
    os << gtid.to_string();
    return os;
}

GtidList::GtidList(const std::vector<Gtid>&& gtids)
    : m_gtids(std::move(gtids))
{
    sort();
    m_is_valid = std::all_of(begin(m_gtids), end(m_gtids), [](const Gtid& gtid) {
        return gtid.is_valid();
    });
}

void GtidList::replace(const Gtid& gtid)
{
    auto ite = std::find_if(begin(m_gtids), end(m_gtids), [&gtid](const Gtid& rhs) {
        return gtid.domain_id() == rhs.domain_id();
    });

    if (ite != end(m_gtids) && ite->domain_id() == gtid.domain_id())
    {
        *ite = gtid;
    }
    else
    {
        m_gtids.push_back(gtid);
        sort();
    }

    m_is_valid = std::all_of(begin(m_gtids), end(m_gtids), [](const Gtid& g) {
        return g.is_valid();
    });
}

std::string GtidList::to_string() const
{
    return maxbase::join(m_gtids);
}

GtidList GtidList::from_string(const std::string& str)
{
    std::vector<Gtid> gvec;

    auto gtid_strs = maxbase::strtok(str, ",");
    for (auto& s : gtid_strs)
    {
        gvec.push_back(Gtid::from_string(s));
    }

    return GtidList(std::move(gvec));
}

void GtidList::sort()
{
    std::sort(begin(m_gtids), end(m_gtids), [](const Gtid& lhs, const Gtid& rhs) {
        return lhs.domain_id() < rhs.domain_id();
    });
}

bool GtidList::is_included(const GtidList& other) const
{
    if (m_gtids.empty())
    {
        return false;
    }

    for (const auto& gtid : other.gtids())
    {
        auto it = std::find_if(
            m_gtids.begin(), m_gtids.end(), [&](const Gtid& g) {
            return g.domain_id() == gtid.domain_id();
        });

        if (it == m_gtids.end() || it->sequence_nr() < gtid.sequence_nr())
        {
            return false;
        }
    }

    return true;
}

bool GtidList::has_domain(uint32_t domain_id) const
{
    return std::any_of(begin(m_gtids), end(m_gtids), [domain_id]
                       (const auto& gtid)
                       {
        return gtid.domain_id() == domain_id;
    });
}

std::ostream& operator<<(std::ostream& os, const GtidList& lst)
{
    os << lst.to_string();
    return os;
}
}
