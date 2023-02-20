/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pinloki.hh"
#include "file_writer.hh"
#include "file_reader.hh"
#include "config.hh"

#include <maxscale/protocol/mariadb/mysql.hh>
#include <mariadb_rpl.h>
#include <iostream>
#include <iomanip>

namespace
{

/** The next file name has the same base name as the file from the master, but
 *    the counter portion is generated here.  By using the master base_name the
 *    event length stays the same, which means the 'next_pos' in the events do
 *    not have to be modified.
 */
std::string next_file_name(const std::string& master, const std::string& prev)
{
    using namespace  std;

    auto base_name = master.substr(0, master.find_last_of('.'));

    auto num = 1;
    if (!prev.empty())
    {
        auto num_str = prev.substr(prev.find_last_of(".") + 1);
        num = 1 + atoi(num_str.c_str());
    }

    return MAKE_STR(base_name << '.' << setfill('0') << setw(6) << num);
}
}

namespace pinloki
{
FileWriter::FileWriter(InventoryWriter* inv, const Writer& writer)
    : m_inventory(*inv)
    , m_writer(writer)
{
}

void FileWriter::begin_txn()
{
    mxb_assert(m_in_transaction == false);
    m_in_transaction = true;
}

void FileWriter::commit_txn()
{
    mxb_assert(m_in_transaction == true);
    m_in_transaction = false;
    flush_buffer();
}

void FileWriter::store_in_buffer(maxsql::RplEvent& rpl_event)
{
    if (m_encrypt)
    {
        std::vector<char> plaintext(rpl_event.pBuffer(), rpl_event.pEnd());
        uint32_t current_pos = m_current_pos.write_pos + m_tx_buffer.size();
        auto encrypted = m_encrypt->encrypt_event(plaintext, current_pos);
        m_tx_buffer.insert(m_tx_buffer.end(), encrypted.begin(), encrypted.end());
    }
    else
    {
        m_tx_buffer.insert(m_tx_buffer.end(), rpl_event.pBuffer(), rpl_event.pEnd());
    }
}

void FileWriter::flush_buffer()
{
    m_current_pos.file.seekp(m_current_pos.write_pos);
    m_current_pos.file.write(m_tx_buffer.data(), m_tx_buffer.size());

    m_current_pos.write_pos = m_current_pos.file.tellp();
    m_current_pos.file.flush();

    m_tx_buffer.clear();

    if (!m_current_pos.file.good())
    {
        MXB_THROW(BinlogWriteError, "Could not write event to " << m_current_pos.name);
    }
}

void FileWriter::rollback_txn()
{
    mxb_assert(m_in_transaction == true);
    m_in_transaction = false;
    m_tx_buffer.clear();
}

void FileWriter::add_event(maxsql::RplEvent& rpl_event)     // FIXME, move into here
{
    auto etype = rpl_event.event_type();

    if (etype == HEARTBEAT_LOG_EVENT)
    {
        // Heartbeat event, don't process it
    }
    else if (etype == ROTATE_EVENT)
    {
        // Regardless if this is steady state or pinloki just started to run there
        // will be an initial ROTATE_EVENT followed by a FORMAT_DESCRIPTION_EVENT
        // which is when the actual rotate will be performed, unless it has already
        // been performed. In the latter case events will be appended to an existing file.
        m_rotate = rpl_event.rotate();
    }
    else
    {
        if (etype == FORMAT_DESCRIPTION_EVENT)
        {
            mxb_assert(m_in_transaction == false);
            mxb_assert(m_rotate.file_name.empty() == false);

            if (!open_for_appending(m_rotate, rpl_event))
            {
                perform_rotate(m_rotate);
            }

            m_rotate.file_name.clear();
        }

        m_ignore_preamble = m_ignore_preamble
            && (rpl_event.event_type() == GTID_LIST_EVENT
                || rpl_event.event_type() == FORMAT_DESCRIPTION_EVENT
                || rpl_event.event_type() == BINLOG_CHECKPOINT_EVENT);


        if (!m_ignore_preamble)
        {
            rpl_event.set_next_pos(m_current_pos.write_pos + rpl_event.buffer_size()
                                   + m_tx_buffer.size());

            if (m_in_transaction)
            {
                store_in_buffer(rpl_event);
            }
            else if (etype == GTID_LIST_EVENT)
            {
                write_gtid_list();
            }
            else if (etype != STOP_EVENT && etype != ROTATE_EVENT && etype != BINLOG_CHECKPOINT_EVENT)
            {
                write_to_file(rpl_event);
            }
        }
    }
}

bool FileWriter::open_for_appending(const maxsql::Rotate& rotate, const maxsql::RplEvent& fmt_event)
{
    if (!m_newborn)
    {
        return false;
    }

    m_newborn = false;

    const auto& file_names = m_inventory.file_names();

    if (file_names.empty())
    {
        return false;
    }

    auto last_file_name = last_string(file_names);

    if (open_binlog(last_file_name, &fmt_event))
    {
        m_ignore_preamble = true;
    }

    return m_ignore_preamble;
}

bool FileWriter::open_binlog(const std::string& file_name, const maxsql::RplEvent* ev)
{
    std::ifstream log_file(file_name);

    if (!log_file)
    {
        return false;
    }

    // Read the first event which is always a format event
    long file_pos = pinloki::PINLOKI_MAGIC.size();
    maxsql::RplEvent event = maxsql::RplEvent::read_event(log_file, &file_pos);
    bool rv = false;

    if (event.event_type() == FORMAT_DESCRIPTION_EVENT
        && (!ev || memcmp(ev->pHeader(), event.pHeader(), mxq::RPL_HEADER_LEN) == 0))
    {
        rv = true;
        m_current_pos.name = file_name;
        m_current_pos.file.open(m_current_pos.name, std::ios_base::in | std::ios_base::out
                                | std::ios_base::binary);
        m_current_pos.file.seekp(0, std::ios_base::end);
        m_current_pos.write_pos = m_current_pos.file.tellp();

        if (auto encrypt = maxsql::RplEvent::read_event(log_file, &file_pos))
        {
            // File has more events after the FDE.
            if (encrypt.event_type() == START_ENCRYPTION_EVENT)
            {
                const auto& cnf = m_inventory.config();
                m_encrypt = mxq::create_encryption_ctx(cnf.key_id(), cnf.encryption_cipher(),
                                                       m_current_pos.name, encrypt);
            }
        }
    }

    return rv;
}

void FileWriter::perform_rotate(const maxsql::Rotate& rotate)
{
    auto master_file_name = rotate.file_name;
    auto last_file_name = last_string(m_inventory.file_names());

    auto new_file_name = next_file_name(master_file_name, last_file_name);
    auto file_name = m_inventory.config().path(new_file_name);

    if (m_current_pos.file.is_open())
    {
        write_rotate(file_name);
    }
    else if (!last_file_name.empty())
    {
        write_stop(last_file_name);
    }

    if (m_current_pos.file.is_open())
    {
        m_current_pos.file.close();

        if (!m_current_pos.file.good())
        {
            MXB_THROW(BinlogWriteError,
                      "File " << m_current_pos.name
                              << " did not close (flush) properly during rotate: "
                              << errno << ", " << mxb_strerror(errno));
        }
    }

    m_current_pos.name = file_name;
    m_current_pos.file.open(m_current_pos.name, std::ios_base::out | std::ios_base::binary);
    m_current_pos.file.write(PINLOKI_MAGIC.data(), PINLOKI_MAGIC.size());
    m_current_pos.write_pos = PINLOKI_MAGIC.size();
    m_current_pos.file.flush();

    m_inventory.push_back(m_current_pos.name);
}

void FileWriter::write_to_file(maxsql::RplEvent& rpl_event)
{
    const std::string& key_id = m_inventory.config().key_id();

    if (rpl_event.event_type() == FORMAT_DESCRIPTION_EVENT && !key_id.empty())
    {
        // Reset the encryption context for every new binlog. Both the FORMAT_DESCRIPTION and the
        // START_ENCRYPTION events must be unencrypted even if the previous file was also encrypted.
        m_encrypt.reset();

        write_plain_to_file(rpl_event.pBuffer(), rpl_event.buffer_size());

        if (auto key_manager = mxs::key_manager())
        {
            auto [ok, vers, key] = key_manager->get_key(key_id);

            if (ok)
            {
                maxsql::RplEvent event(
                    mxq::create_start_encryption_event(rpl_event.server_id(), vers, m_current_pos.write_pos));

                write_plain_to_file(event.pBuffer(), event.buffer_size());

                auto start_encryption = event.start_encryption_event();
                const auto cipher = m_inventory.config().encryption_cipher();
                m_encrypt = std::make_unique<mxq::EncryptCtx>(cipher, key, start_encryption.iv);
            }
            else
            {
                MXB_THROW(mxq::EncryptionError, "Failed to open encryption key '" << key_id << "'.");
            }
        }
        else
        {
            MXB_THROW(mxq::EncryptionError,
                      "Encryption key ID is set to '" << key_id << "' but key manager is not enabled. "
                                                      << "Cannot write encrypted binlog files.");
        }
    }
    else if (m_encrypt)
    {
        // All other events in the binlog are encrypted
        mxb_assert(!key_id.empty());

        std::vector<char> plaintext(rpl_event.pBuffer(), rpl_event.pEnd());
        auto encrypted = m_encrypt->encrypt_event(plaintext, m_current_pos.write_pos);
        write_plain_to_file(encrypted.data(), encrypted.size());
    }
    else
    {
        write_plain_to_file(rpl_event.pBuffer(), rpl_event.buffer_size());
    }
}

void FileWriter::write_plain_to_file(const char* ptr, size_t bytes)
{
    m_current_pos.file.seekp(m_current_pos.write_pos);
    m_current_pos.file.write(ptr, bytes);

    m_current_pos.write_pos = m_current_pos.file.tellp();
    m_current_pos.file.flush();

    if (!m_current_pos.file.good())
    {
        MXB_THROW(BinlogWriteError, "Could not write event to " << m_current_pos.name);
    }
}

void FileWriter::write_stop(const std::string& file_name)
{
    MXB_SINFO("write stop to " << file_name);
    mxb_assert(!m_current_pos.file.is_open());

    if (!open_binlog(file_name) || !m_current_pos.file.good())
    {
        MXB_THROW(BinlogWriteError,
                  "Could not open " << file_name << " for  STOP_EVENT addition");
    }

    constexpr int HEADER_LEN = 19;
    const size_t EVENT_LEN = HEADER_LEN + 4;        // header plus crc

    std::vector<char> data(EVENT_LEN);
    uint8_t* ptr = (uint8_t*)&data[0];

    // Zero timestamp
    mariadb::set_byte4(ptr, 0);
    ptr += 4;

    // A stop event
    *ptr++ = STOP_EVENT;

    // server id
    mariadb::set_byte4(ptr, m_inventory.config().server_id());
    ptr += 4;

    // Event length
    mariadb::set_byte4(ptr, EVENT_LEN);
    ptr += 4;

    // Next position
    mariadb::set_byte4(ptr, m_current_pos.write_pos + EVENT_LEN);
    ptr += 4;

    // No flags (this is a real event)
    mariadb::set_byte2(ptr, 0);
    ptr += 2;

    // Checksum
    mariadb::set_byte4(ptr, crc32(0, (uint8_t*)data.data(), data.size() - 4));

    mxq::RplEvent event(std::move(data));
    write_to_file(event);
}

void FileWriter::write_rotate(const std::string& to_file_name)
{
    auto vec = maxsql::create_rotate_event(basename(to_file_name.c_str()),
                                           m_inventory.config().server_id(),
                                           m_current_pos.write_pos,
                                           mxq::Kind::Real);

    mxq::RplEvent event(std::move(vec));
    write_to_file(event);
}

void FileWriter::write_gtid_list()
{
    constexpr int HEADER_LEN = 19;
    auto gtid_list = m_writer.get_gtid_io_pos();
    const auto NUM_GTIDS = gtid_list.gtids().size();
    const size_t EVENT_LEN = HEADER_LEN + 4 + NUM_GTIDS * (4 + 4 + 8) + 4;

    std::vector<char> data(EVENT_LEN);
    uint8_t* ptr = (uint8_t*)&data[0];

    // Zero timestamp
    mariadb::set_byte4(ptr, 0);
    ptr += 4;

    // The event
    *ptr++ = GTID_LIST_EVENT;

    // server id
    mariadb::set_byte4(ptr, m_inventory.config().server_id());
    ptr += 4;

    // Event length
    mariadb::set_byte4(ptr, EVENT_LEN);
    ptr += 4;

    // Next position
    mariadb::set_byte4(ptr, m_current_pos.write_pos + EVENT_LEN);
    ptr += 4;

    // No flags (this is a real event)
    mariadb::set_byte2(ptr, 0);
    ptr += 2;

    // Number of gtids to follow:
    mariadb::set_byte4(ptr, NUM_GTIDS);
    ptr += 4;
    // Gtids:
    for (const auto& gtid : gtid_list.gtids())
    {
        mariadb::set_byte4(ptr, gtid.domain_id());
        ptr += 4;
        mariadb::set_byte4(ptr, gtid.server_id());
        ptr += 4;
        mariadb::set_byte8(ptr, gtid.sequence_nr());
        ptr += 8;
    }


    // Checksum
    mariadb::set_byte4(ptr, crc32(0, (uint8_t*)data.data(), data.size() - 4));

    mxq::RplEvent event(std::move(data));
    write_to_file(event);
}
}
