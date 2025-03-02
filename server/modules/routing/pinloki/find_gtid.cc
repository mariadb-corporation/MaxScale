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

    auto lhs_stripped = strip_extension(std::string_view {lhs.file_name}, COMPRESSION_EXTENSION);
    auto lhs_pos = lhs_stripped.find_last_of(".");
    auto lhs_seqnr = std::atoi(&lhs.file_name[lhs_pos + 1]);

    auto rhs_stripped = strip_extension(std::string_view {rhs.file_name}, COMPRESSION_EXTENSION);
    auto rhs_pos = rhs_stripped.find_last_of(".");
    auto rhs_seqnr = std::atoi(&rhs.file_name[rhs_pos + 1]);

    return lhs_seqnr < rhs_seqnr || (lhs_seqnr == rhs_seqnr && lhs.file_pos < rhs.file_pos);
}

std::vector<GtidPosition> search_file(const std::string& file_name,
                                      std::vector<maxsql::Gtid> gtids,
                                      const Config& cnf);


std::vector<GtidPosition> find_gtid_position(const std::vector<maxsql::Gtid>& gtids_,
                                             const Config& cnf)
{
    // The gtids from the replica are the ones it already has,
    // search for the next ones in sequence.
    auto gtids{gtids_};
    for (auto& gtid : gtids)
    {
        gtid.inc_seq();
    }

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

    // Adjust the gtids to what is expected elsewhere,
    // which is that the gtids are the ones the replica already has.
    // If would be clearer if the method everywhere was that the
    // gtids are the next ones to fetch, but that change is for later.
    for (auto& gpos : ret)
    {
        gpos.gtid.dec_seq();
    }

    return ret;
}

// TODO don't hog the CPU, and add an absolut time limit
// TODO in file_reader, handle missing gtid: it can be in the future
//      (files deleted from the tail) or deleted (files deleted from the head)
std::vector<GtidPosition> search_file(const std::string& file_name,
                                      std::vector<maxsql::Gtid> gtids,
                                      const Config& cnf)
{
    auto sBinlog = cnf.shared_binlog_file().binlog_file(file_name);
    IFStreamReader file(sBinlog->make_ifstream());
    auto nbytes = file.advance_for(MAGIC_SIZE, 5s);
    if (nbytes != MAGIC_SIZE)
    {
        MXB_THROW(GtidSearchTimeout, "Timeout reading " << file_name);
    }

    std::vector<GtidPosition> ret;
    maxsql::GtidList gtid_list;
    std::unique_ptr<mxq::EncryptCtx> encrypt;

    for (bool done = false; !done;)
    {
        // If the file is being decompressed it can lag behind reading. This is
        // the only case where events need to be seen without interruption until
        // gtids are found or the file actually ends (STOP or ROTATE)
        auto read_pos = file.bytes_read();
        auto is_decompressing = sBinlog->check_compression_status();
        maxsql::RplEvent rpl = mxq::RplEvent::read_event(file, encrypt);

        if (!rpl)
        {
            done = !is_decompressing;
            continue;
        }

        switch (rpl.event_type())
        {
        case START_ENCRYPTION_EVENT:
            encrypt = mxq::create_encryption_ctx(cnf.key_id(), cnf.encryption_cipher(), file_name, rpl);
            break;

        case GTID_LIST_EVENT:
            {
                gtid_list = rpl.gtid_list().gtid_list;
                // If a gtid being searched is in the domain of one of the gtids in the list, and
                // the one in the list is later than the gtid being searched, it is in a prior file.
                auto itr = std::find_if(gtids.begin(), gtids.end(), [&gtid_list](const auto& s) {
                    auto dg = gtid_list.domain_gtid(s.domain_id());
                    return dg.is_valid() && dg.sequence_nr() > s.sequence_nr();
                });

                if (itr != gtids.end())
                {
                    gtids.erase(itr);   // The gtid is in a prior file and will be searched for.
                }
            }
            break;

        case GTID_EVENT:
            {
                auto gtid = rpl.gtid_event().gtid;
                if (auto itr = std::find(gtids.begin(), gtids.end(), gtid); itr != gtids.end())
                {   // exact match. The replica already has this gtid so it will not actually be sent.
                    ret.emplace_back(*itr, file_name, read_pos);
                    gtids.erase(itr);
                }
            }
            break;

        case STOP_EVENT:
        case ROTATE_EVENT:
            done = true;
            break;

        default:
            // ignore
            break;
        }

        if (gtids.empty())
        {
            break;
        }
    }

    // If a gtid with its domain in the gtid_list was not found
    // it is a future gtid.
    for (const auto& gtid : gtids)
    {
        if (gtid_list.has_domain(gtid.domain_id()))
        {
            ret.emplace_back(gtid, file_name, file.bytes_read());
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
        auto decompressed_name = strip_extension(file_name, COMPRESSION_EXTENSION);

        auto in_file = std::ifstream(file_name);
        auto out_file = std::ofstream(std::string {decompressed_name});

        maxbase::Decompressor decomp;
        auto stat = decomp.decompress(in_file, out_file);

        if (stat == mxb::CompressionStatus::OK)
        {
            ::remove(file_name.c_str());
            file_name = decompressed_name;
        }
        else
        {
            ::remove(std::string {decompressed_name}.c_str());
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
