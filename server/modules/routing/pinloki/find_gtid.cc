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

#include "find_gtid.hh"
#include "inventory.hh"
#include "pinloki.hh"
#include "rpl_event.hh"
#include <maxbase/log.hh>
#include <fstream>
#include <iostream>
#include <iomanip>

// TODO searching all files. It should not be an error that a file cannot be opened! Old files
// might be purged. Need to think how that should be handled.

namespace pinloki
{
bool search_file(const std::string& file_name,
                 const maxsql::Gtid& gtid,
                 GtidPosition* pos,
                 bool search_in_file);

GtidPosition find_gtid_position(const maxsql::Gtid& gtid, const Inventory* inv)
{
    // Simple linear search. If there can be a lot of files, make this a binary search, or
    // if it really becomes slow, create an index
    GtidPosition pos;
    const auto& file_names = inv->file_names();

    // Search in reverse because the gtid is likely be one of the latest files, and
    // the search can stop as soon as the gtid is greater than the gtid list in the file,
    // uh, expect for the first file which doesn't have a GTID_LIST_EVENT.

    auto last_one = rend(file_names) - 1;   // which is the first, oldest file
    for (auto ite = rbegin(file_names); ite != rend(file_names); ++ite)
    {
        if (search_file(*ite, gtid, &pos, ite == last_one))
        {
            break;
        }
    }

    return pos;
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

    long file_pos = PINLOKI_MAGIC.size();

    file.seekg(file_pos);
    // Special rule: if it is the first file, it has no gtid list and we search for the gtid.
    // If old files have been purged, there is a gtid list.
    bool found_file = first_file;

    while (!found_file)
    {
        maxsql::RplEvent rpl = maxsql::read_event(file, &file_pos);
        if (rpl.is_empty())
        {
            break;
        }

        if (rpl.event_type() == GTID_LIST_EVENT)
        {
            maxsql::GtidListEvent event = rpl.gtid_list();

            for (const auto& tid : event.gtid_list.gtids())
            {
                if (tid.domain_id() == gtid.domain_id()
                    && tid.sequence_nr() <= gtid.sequence_nr())
                {
                    found_file = true;      // this is the file, the gtid should be here
                }
            }
        }
    }

    long gtid_pos = 0;
    if (found_file)
    {
        gtid_pos = search_gtid_in_file(file, file_pos, gtid);
        if (gtid_pos)
        {
            ret_pos->file_name = file_name;
            ret_pos->file_pos = gtid_pos;
        }
        else
        {
            if (found_file && !gtid_pos)
            {
                MXB_SERROR("The gtid " << gtid << " should have been in " << file_name
                                       << " but was not found");
            }
        }
    }

    return gtid_pos;
}
}
