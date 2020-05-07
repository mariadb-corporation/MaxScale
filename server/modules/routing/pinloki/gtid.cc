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

#define BOOST_ERROR_CODE_HEADER_ONLY 1

#include "gtid.hh"
#include <maxbase/string.hh>
#include <maxbase/log.hh>
#include <algorithm>
#include <sstream>
#include <mysql.h>
#include <mariadb_rpl.h>
#include <iostream>
#include <iomanip>

namespace maxsql
{

Gtid::Gtid(st_mariadb_gtid* mgtid)
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
    auto values = mxb::strtok(gtid_str, "-");

    if (values.size() == 3)
    {
        return Gtid(std::stoul(values[0]), std::stoul(values[1]), std::stoul(values[2]));
    }
    else
    {
        MXS_SERROR("Invalid gtid string: '" << gtid_str);
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

void GtidList::clear()
{
    m_gtids.clear();
    m_is_valid = false;
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

    m_is_valid = std::all_of(begin(m_gtids), end(m_gtids), [](const Gtid& gtid) {
                                 return gtid.is_valid();
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

std::ostream& operator<<(std::ostream& os, const GtidList& lst)
{
    os << lst.to_string();
    return os;
}
}
