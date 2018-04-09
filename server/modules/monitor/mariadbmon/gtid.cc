/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "gtid.hh"

#include <algorithm>
#include <sstream>
#include "utilities.hh"

using std::string;

GtidList GtidList::from_string(const string& gtid_string)
{
    ss_dassert(gtid_string.size());
    GtidList rval;
    bool error = false;
    bool have_more = false;
    const char* str = gtid_string.c_str();
    do
    {
        char* endptr = NULL;
        auto new_triplet = Gtid::from_string(str, &endptr);
        if (new_triplet.m_server_id == SERVER_ID_UNKNOWN)
        {
            error = true;
        }
        else
        {
            rval.m_triplets.push_back(new_triplet);
            // The last number must be followed by ',' (another triplet) or \0 (last triplet)
            if (*endptr == ',')
            {
                have_more = true;
                str = endptr + 1;
            }
            else if (*endptr == '\0')
            {
                have_more = false;
            }
            else
            {
                error = true;
            }
        }
    } while (have_more && !error);

    if (error)
    {
        // If error occurred, clear the gtid as something is very wrong.
        rval.m_triplets.clear();
    }
    else
    {
        // Usually the servers gives the triplets ordered by domain id:s, but this is not 100%.
        std::sort(rval.m_triplets.begin(), rval.m_triplets.end(), Gtid::compare_domains);
    }
    return rval;
}

string GtidList::to_string() const
{
    string rval;
    string separator;
    for (auto iter = m_triplets.begin(); iter != m_triplets.end(); iter++)
    {
        rval += separator + iter->to_string();
        separator = ",";
    }
    return rval;
}

bool GtidList::can_replicate_from(const GtidList& master_gtid)
{
    /* The result of this function is false if the source and master have a common domain id where
     * the source is ahead of the master. */
    return (events_ahead(*this, master_gtid, MISSING_DOMAIN_IGNORE) == 0);
}

bool GtidList::empty() const
{
    return m_triplets.empty();
}

bool GtidList::operator == (const GtidList& rhs) const
{
    return m_triplets == rhs.m_triplets;
}

uint64_t GtidList::events_ahead(const GtidList& lhs, const GtidList& rhs, substraction_mode_t domain_substraction_mode)
{
    const size_t n_lhs = lhs.m_triplets.size();
    const size_t n_rhs = rhs.m_triplets.size();
    size_t ind_lhs = 0, ind_rhs = 0;
    uint64_t events = 0;

    while (ind_lhs < n_lhs && ind_rhs < n_rhs)
    {
        auto lhs_triplet = lhs.m_triplets[ind_lhs];
        auto rhs_triplet = rhs.m_triplets[ind_rhs];
        // Server id -1 should never be saved in a real gtid variable.
        ss_dassert(lhs_triplet.m_server_id != SERVER_ID_UNKNOWN &&
                   rhs_triplet.m_server_id != SERVER_ID_UNKNOWN);
        // Search for matching domain_id:s, advance the smaller one.
        if (lhs_triplet.m_domain < rhs_triplet.m_domain)
        {
            if (domain_substraction_mode == MISSING_DOMAIN_LHS_ADD)
            {
                // The domain on lhs does not exist on rhs. Add entire sequence number of lhs to the result.
                events += lhs_triplet.m_sequence;
            }
            ind_lhs++;
        }
        else if (lhs_triplet.m_domain > rhs_triplet.m_domain)
        {
            ind_rhs++;
        }
        else
        {
            // Domains match, check sequences.
            if (lhs_triplet.m_sequence > rhs_triplet.m_sequence)
            {
                /* Same domains, but lhs sequence is equal or ahead of rhs sequence.  */
                events += lhs_triplet.m_sequence - rhs_triplet.m_sequence;
            }
            // Continue to next domains.
            ind_lhs++;
            ind_rhs++;
        }
    }
    return events;
}

Gtid Gtid::from_string(const char* str, char** endptr)
{
    /* Error checking the gtid string is a bit questionable, as having an error means that the server is
       buggy or network has faults, in which case nothing can be trusted. But without error checking
       MaxScale may crash if string is wrong. */
    ss_dassert(endptr);
    const char* ptr = str;
    char* strtoull_endptr = NULL;
    // Parse three numbers separated by -
    uint64_t parsed_numbers[3];
    bool error = false;
    for (int i = 0; i < 3 && !error; i++)
    {
        errno = 0;
        parsed_numbers[i] = strtoull(ptr, &strtoull_endptr, 10);
        // No parse error
        if (errno != 0)
        {
            error = true;
        }
        else if (i < 2)
        {
            // First two numbers must be followed by a -
            if (*strtoull_endptr == '-')
            {
                ptr = strtoull_endptr + 1;
            }
            else
            {
                error = true;
            }
        }
    }

    // Check that none of the parsed numbers are unexpectedly large. This shouldn't really be possible unless
    // server has a bug or network had an error.
    if (!error && (parsed_numbers[0] > UINT32_MAX || parsed_numbers[1] > UINT32_MAX))
    {
        error = true;
    }

    if (!error)
    {
        *endptr = strtoull_endptr;
        return Gtid((uint32_t)parsed_numbers[0], parsed_numbers[1], parsed_numbers[2]);
    }
    else
    {
        return Gtid();
    }
}

Gtid::Gtid()
    : m_domain(0)
    , m_server_id(SERVER_ID_UNKNOWN)
    , m_sequence(0)
{}

Gtid::Gtid(uint32_t domain, int64_t server_id, uint64_t sequence)
    : m_domain(domain)
    , m_server_id(server_id)
    , m_sequence(sequence)
{}

bool Gtid::eq(const Gtid& rhs) const
{
    return m_domain == rhs.m_domain && m_server_id == rhs.m_server_id && m_sequence == rhs.m_sequence;
}

string Gtid::to_string() const
{
    std::stringstream ss;
    if (m_server_id != SERVER_ID_UNKNOWN)
    {
        ss << m_domain << "-" << m_server_id << "-" << m_sequence;
    }
    return ss.str();
}

Gtid GtidList::get_gtid(uint32_t domain) const
{
    Gtid rval;
    // Make a dummy triplet for the domain search
    Gtid search_val(domain, -1, 0);
    auto found = std::lower_bound(m_triplets.begin(), m_triplets.end(), search_val,
                                      Gtid::compare_domains);
    if (found != m_triplets.end() && found->m_domain == domain)
    {
        rval = *found;
    }
    return rval;
}