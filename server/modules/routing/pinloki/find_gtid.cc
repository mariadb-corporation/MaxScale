/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "find_gtid.hh"
#include "inventory.hh"
#include "pinloki.hh"
#include "rpl_event.hh"
#include "binlog_file.hh"
#include <maxbase/log.hh>
#include <maxscale/routingworker.hh>
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

std::vector<GtidPosition> search_file(const std::string& file_name,
                                      const std::vector<maxsql::Gtid>& gtids,
                                      const Config& cnf);


std::vector<GtidPosition> find_gtid_position(std::vector<maxsql::Gtid> gtids,
                                             const Config& cnf)
{
    mxb::WatchdogNotifier::Workaround workaround(mxs::RoutingWorker::get_current());

    std::vector<GtidPosition> ret;
    // Simple linear search. If there can be a lot of files, make this a binary search, or
    // if it really becomes slow, create an index
    const auto& file_names = cnf.binlog_file_names();

    // Search files in reverse because the gtids are likely be in one of the latest files,
    // and the search can stop as soon as the gtid is greater than the gtid list in the file.

    for (auto ite = rbegin(file_names); ite != rend(file_names); ++ite)
    {
        auto positions = search_file(*ite, gtids, cnf);

        for (const auto& pos : positions)
        {
            auto gite = std::find(begin(gtids), end(gtids), pos.gtid);
            mxb_assert(gite != end(gtids));
            gtids.erase(gite);
            ret.push_back(std::move(pos));
        }

        if (gtids.empty())
        {
            break;
        }
    }

    // Any remaining gtids that were not found.
    for (const auto& g : gtids)
    {
        ret.emplace_back(g, ""s, 0);
    }

    sort(begin(ret), end(ret));

    return ret;
}

maxsql::GtidList get_gtid_list(const std::string& file_name,
                               const Config& cnf)
{
    auto sBinlog = cnf.shared_binlog_file().binlog_file(file_name);
    IFStreamReader file(sBinlog->make_ifstream());
    auto nbytes = file.advance_for(MAGIC_SIZE, 10ms);
    if (nbytes != MAGIC_SIZE)
    {
        MXB_THROW(BinlogReadError, "Failed to read '" << file_name
                                                 << "' :" << errno << ", " << mxb_strerror(errno));
    }

    maxsql::GtidList gtid_list;
    std::unique_ptr<mxq::EncryptCtx> encrypt;

    while (maxsql::RplEvent rpl = mxq::RplEvent::read_event(file, encrypt))
    {
        if (rpl.event_type() == START_ENCRYPTION_EVENT)
        {
            encrypt = mxq::create_encryption_ctx(cnf.key_id(), cnf.encryption_cipher(), file_name, rpl);
        }
        else if (rpl.event_type() == GTID_LIST_EVENT)
        {
            maxsql::GtidListEvent event = rpl.gtid_list();
            gtid_list = event.gtid_list;

            // There is only one gtid list in a file. If the list was empty, this
            // is the very first binlog file, continue looping and reading GTIDS
            // to build an artificial gtid list.
            if (!event.gtid_list.gtids().empty())
            {
                break;
            }
        }
        else if (rpl.event_type() == GTID_EVENT)
        {
            maxsql::GtidEvent event = rpl.gtid_event();
            if (!gtid_list.has_domain(event.gtid.domain_id()))
            {
                maxsql::Gtid gtid2{event.gtid.domain_id(),
                                   event.gtid.server_id(),
                                   event.gtid.sequence_nr() - 1};
                gtid_list.replace(gtid2);
            }
        }
    }

    return gtid_list;
}

std::vector<GtidPosition> search_file(const std::string& file_name,
                                      const std::vector<maxsql::Gtid>& gtids,
                                      const Config& cnf)
{
    std::vector<GtidPosition> ret;

    auto gtid_list = get_gtid_list(file_name, cnf);

    for (const auto& list_gtid : gtid_list.gtids())
    {
        for (const auto& search_gtid : gtids)
        {
            if (list_gtid.domain_id() == search_gtid.domain_id()
                && list_gtid.sequence_nr() <= search_gtid.sequence_nr())
            {
                ret.emplace_back(search_gtid, file_name, MAGIC_SIZE);
            }
        }
    }

    return ret;
}

maxsql::GtidList find_last_gtid_list(const Config& cnf)
{
    maxsql::GtidList ret;
    if (cnf.binlog_file_names().empty())
    {
        return ret;
    }

    auto file_name = cnf.binlog_file_names().back();
    IFStreamReader file {file_name};
    file.advance(MAGIC_SIZE);
    long file_pos = MAGIC_SIZE;
    mxb_assert(file.at_pos(file_pos));

    long prev_pos = file_pos;
    long truncate_to = 0;
    bool in_trx = false;
    mxq::Gtid last_gtid;
    uint8_t flags = 0;
    std::unique_ptr<mxq::EncryptCtx> encrypt_ctx;

    while (auto rpl = mxq::RplEvent::read_event(file, encrypt_ctx))
    {
        switch (rpl.event_type())
        {
        case START_ENCRYPTION_EVENT:
            encrypt_ctx = mxq::create_encryption_ctx(cnf.key_id(), cnf.encryption_cipher(),
                                                     file_name, rpl);
            break;

        case GTID_LIST_EVENT:
            {
                auto event = rpl.gtid_list();

                for (const auto& gtid : event.gtid_list.gtids())
                {
                    ret.replace(gtid);
                }
            }
            break;

        case GTID_EVENT:
            {
                auto event = rpl.gtid_event();
                in_trx = true;
                truncate_to = prev_pos;
                flags = event.flags;
                last_gtid = event.gtid;
            }
            break;

        case XID_EVENT:
            in_trx = false;
            ret.replace(last_gtid);
            break;

        case QUERY_EVENT:
            // This was a DDL event that commits the previous transaction. If the F_STANDALONE flag is not
            // set, an XID_EVENT will follow that commits the transaction.
            if (flags & mxq::F_STANDALONE)
            {
                in_trx = false;
                ret.replace(last_gtid);
            }
            break;

        case STOP_EVENT:
        case ROTATE_EVENT:
            // End of the binlog, return the latest GTID we found. We can assume that only complete
            // transactions are stored in the file if we get this far.
            return ret;

        default:
            MXB_SDEBUG("GTID search: " << rpl);
            break;
        }

        if (prev_pos < rpl.next_event_pos())
        {
            file_pos = rpl.next_event_pos();
        }
        else
        {
            // If the binlog file is over 4GiB, we cannot rely on the next event offset anymore.
            file_pos = prev_pos + rpl.buffer_size();
            mxb_assert(file_pos >= std::numeric_limits<uint32_t>::max());
        }

        prev_pos = file_pos;
    }

    if (!ret.is_empty() && in_trx)
    {
        MXB_WARNING("Partial transaction '%s' in '%s'. Truncating the file to "
                    "the last known good event at %ld.",
                    last_gtid.to_string().c_str(), file_name.c_str(), truncate_to);

        // NOTE: If the binlog file is ever read by multiple independent readers in parallel, file truncation
        // cannot be done. Instead of truncating the file, a separate temporary file that holds the
        // partially replicated transactions needs to be used.
        if (truncate(file_name.c_str(), truncate_to) != 0)
        {
            MXB_ERROR("Failed to truncate '%s': %d, %s", file_name.c_str(), errno, mxb_strerror(errno));
        }
    }

    return ret;
}
}
