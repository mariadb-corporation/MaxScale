/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "find_gtid.hh"
#include "inventory.hh"
#include "pinloki.hh"
#include "rpl_event.hh"
#include <maxbase/log.hh>
#include <fstream>
#include <iostream>
#include <iomanip>

namespace pinloki
{
inline bool operator<(const GtidPosition& lhs, const GtidPosition& rhs)
{
    if (lhs.file_name.empty())
    {
        return true;
    }
    else if (rhs.file_name.empty())
    {
        return false;
    }

    auto lhs_pos = lhs.file_name.find_last_of(".");
    auto rhs_pos = lhs.file_name.find_last_of(".");

    auto lhs_num = std::atoi(&lhs.file_name[lhs_pos + 1]);
    auto rhs_num = std::atoi(&rhs.file_name[rhs_pos + 1]);

    return lhs_num < rhs_num || (lhs_num == rhs_num && lhs.file_pos < rhs.file_pos);
}

bool search_file(const std::string& file_name,
                 const maxsql::Gtid& gtid,
                 GtidPosition* pos,
                 bool search_in_file);

std::vector<GtidPosition> find_gtid_position(const std::vector<maxsql::Gtid>& gtids,
                                             const InventoryReader& inv)
{
    std::vector<GtidPosition> ret;
    // Simple linear search. If there can be a lot of files, make this a binary search, or
    // if it really becomes slow, create an index
    const auto& file_names = inv.file_names();

    // Search in reverse because the gtid is likely be one of the latest files, and
    // the search can stop as soon as the gtid is greater than the gtid list in the file,
    // uh, expect for the first file which doesn't have a GTID_LIST_EVENT.

    // TODO, don't do one gtid at a time, modify the search to do all in one go.
    for (const auto& gtid : gtids)
    {
        GtidPosition pos {gtid};
        auto last_one = rend(file_names) - 1;   // which is the first, oldest file
        for (auto ite = rbegin(file_names); ite != rend(file_names); ++ite)
        {
            if (search_file(*ite, gtid, &pos, ite == last_one))
            {
                break;
            }
        }

        ret.push_back(pos);
    }

    sort(begin(ret), end(ret));

    return ret;
}

/**
 * @brief search_gtid_in_file
 * @param file
 * @param from_pos
 * @return position, or 0 if not found
 */
long search_gtid_in_file(std::ifstream& file, long file_pos, const maxsql::Gtid& gtid)
{
    long found_pos = 0;

    while (!found_pos)
    {
        auto this_pos = file_pos;

        maxsql::RplEvent rpl = maxsql::read_event(file, &file_pos);
        if (rpl.is_empty())
        {
            break;
        }

        if (rpl.event_type() == GTID_EVENT)
        {
            maxsql::GtidEvent event = rpl.gtid_event();
            if (event.gtid.domain_id() == gtid.domain_id()
                && event.gtid.sequence_nr() == gtid.sequence_nr())
            {
                found_pos = this_pos;
            }
        }
    }

    return found_pos;
}

bool search_file(const std::string& file_name,
                 const maxsql::Gtid& gtid,
                 GtidPosition* ret_pos,
                 bool first_file)
{
    std::ifstream file {file_name, std::ios_base::in | std::ios_base::binary};

    if (!file.good())
    {
        MXB_SERROR("Could not open binlog file " << file_name);
        return false;
    }

    enum GtidListResult {NotFound, GtidInThisFile, GtidInPriorFile};
    GtidListResult result = NotFound;
    long file_pos = PINLOKI_MAGIC.size();

    while (result == NotFound)
    {
        maxsql::RplEvent rpl = maxsql::read_event(file, &file_pos);

        if (rpl.is_empty())
        {
            break;
        }

        if (rpl.event_type() == GTID_LIST_EVENT)
        {
            maxsql::GtidListEvent event = rpl.gtid_list();

            uint32_t highest_seq = 0;
            bool domain_in_list = false;

            for (const auto& tid : event.gtid_list.gtids())
            {
                if (tid.domain_id() == gtid.domain_id())
                {
                    domain_in_list = true;
                    highest_seq = std::max(highest_seq, tid.sequence_nr());
                }
            }

            if (!domain_in_list || (domain_in_list && highest_seq < gtid.sequence_nr()))
            {
                result = GtidInThisFile;
            }
            else if (highest_seq == gtid.sequence_nr())
            {
                result = GtidInPriorFile;
            }
        }
    }

    bool success = false;

    // The first file does not necessarily have a GtidList
    if ((result == NotFound && first_file) || result == GtidInThisFile)
    {
        if (result == NotFound)
        {
            file_pos = PINLOKI_MAGIC.size();
        }

        file.clear();
        file_pos = search_gtid_in_file(file, file_pos, gtid);
        if (file_pos)
        {
            success = true;
            ret_pos->file_name = file_name;
            ret_pos->file_pos = file_pos;
        }
    }
    else if (result == GtidInPriorFile)
    {
        // The gtid is in a prior log file, and the caller already has it.
        // file_pos points one past the gtid list, but to be sure the whole file
        // is always sent, let the reader handle positioning.
        success = true;
        ret_pos->file_name = file_name;
        ret_pos->file_pos = PINLOKI_MAGIC.size();
    }

    return success;
}
}
