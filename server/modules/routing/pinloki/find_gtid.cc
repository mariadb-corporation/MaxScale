/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
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

GtidPosition find_gtid_position(const maxsql::Gtid& gtid, const InventoryReader& inv)
{
    // Simple linear search. If there can be a lot of files, make this a binary search, or
    // if it really becomes slow, create an index
    GtidPosition pos;
    const auto& file_names = inv.file_names();

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
                if (event.flags & mxq::F_STANDALONE)
                {
                    // Skip the next event
                    rpl = maxsql::read_event(file, &file_pos);
                    found_pos = rpl.next_event_pos();
                }
                else
                {
                    do
                    {
                        rpl = maxsql::read_event(file, &file_pos);
                    }
                    while (rpl.event_type() != XID_EVENT
                           && rpl.is_commit());

                    found_pos = rpl.next_event_pos();
                }
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

    // If this is the first file, skip gtid-list search as there might not be a gtid-list
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

            if (event.gtid_list.gtids().empty())
            {
                found_file = true;          // Possibly found file
            }
            else
            {
                for (const auto& tid : event.gtid_list.gtids())
                {
                    if (tid.domain_id() == gtid.domain_id()
                        && tid.sequence_nr() < gtid.sequence_nr())
                    {
                        // The gtid should be in this file, unless the master has rebooted,
                        // in which case there can be several files with the same gtid list.
                        found_file = true;
                    }
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
    }

    return gtid_pos;
}
}
