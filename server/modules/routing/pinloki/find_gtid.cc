/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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
    auto rhs_pos = rhs.file_name.find_last_of(".");

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
    std::vector<GtidPosition> ret;
    // Simple linear search. If there can be a lot of files, make this a binary search, or
    // if it really becomes slow, create an index
    const auto& file_names = cnf.binlog_file_names();

    // Search files in reverse because the gtids are likely be in one of the latest files,
    // and the search can stop as soon as the gtid is greater than the gtid list in the file.
    // TODO change this to a binary search.

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
    auto nbytes = file.advance_for(MAGIC_SIZE, 5s);
    if (nbytes != MAGIC_SIZE)
    {
        MXB_THROW(GtidSearchTimeout, "Timeout reading " << file_name);
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

    if (has_extension(file_name, COMPRESSION_EXTENSION))
    {   // Decompress in place. This allows a user to take maxscale down
        // and delete tail binlogs. A drastic mesure when something goes
        // wrong with binlogs.
        auto decompressed_name = file_name;
        strip_extension(decompressed_name, COMPRESSION_EXTENSION);

        auto in_file = std::ifstream(file_name);
        auto out_file = std::ofstream(decompressed_name);

        maxbase::Decompressor decomp;
        auto stat = decomp.decompress(in_file, out_file);

        if (stat == mxb::CompressionStatus::OK)
        {
            ::remove(file_name.c_str());
            file_name = decompressed_name;
        }
        else
        {
            ::remove(decompressed_name.c_str());
            MXB_THROW(BinlogReadError, "Failed to decompress '"
                      << file_name << "' :" << errno << ", " << mxb_strerror(errno));
        }
    }

    IFStreamReader file {file_name};
    file.advance(MAGIC_SIZE);
    long file_pos = MAGIC_SIZE;
    mxb_assert(file.at_pos(file_pos));

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
                ret.replace(event.gtid);
            }
            break;

        case STOP_EVENT:
        case ROTATE_EVENT:
            break;

        default:
            MXB_SDEBUG("GTID search: " << rpl);
        }
    }

    return ret;
}
}
