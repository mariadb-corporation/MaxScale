/*
 * Copyright (c) 2024 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "transaction.hh"
#include "inventory.hh"
#include "trx_test.hh"
#include <maxbase/assert.h>
#include <ftw.h>

namespace
{
int rm_cb(const char* fpath, const struct stat* sb, int typeflag, struct FTW* ftwbuf)
{
    if (ftwbuf->level > 0)
    {
        return remove(fpath);
    }

    return 0;
}

int remove_dir_contents(const char* dir)
{
    return nftw(dir, rm_cb, 1024, FTW_DEPTH | FTW_PHYS);
}

// Append the entire input file to the output file starting at out_offset, allowing
// to overwrite part of the output file (even a part in the middle). The default
// (-1) appends as expected.
// The output file, ofs, will have tellp() at one passed the position of the
// last byte written.
void append_file(std::ifstream& ifs, std::ofstream& ofs, int64_t out_offset = -1)
{
    if (out_offset >= 0)
    {
        ofs.seekp(out_offset, std::ios_base::beg);
    }
    else
    {
        ofs.seekp(0, std::ios_base::end);
    }

    ifs.seekg(0, std::ios_base::beg);
    ofs << ifs.rdbuf();
    ofs.flush();
}

bool comp_paths(const std::string& p1, const std::string& p2)
{
    return clean_up_pathname(p1) == clean_up_pathname(p2);
}
}

namespace pinloki
{

/** Class to handle writing a trx to temporary files when a trx didn't fit in
 *  memory. There are two files "trx-binlog" and "summary" in a "trx" directory.
 *  trx-binlog contains the raw data, summary is created and written to on commit.
 *  The summary file contains the starting file position of the transaction in
 *  the target binlog file (and more for validating that the trx belongs to the target,
 *  someone might have deleted binlogs manually).
 *
 *  Recovery works as follows:
 *  1. If there is NO valid summary, delete the contents of the trx directory and return
 *  2. Open the target binlog for appending
 *  3. tellg() tells the current file position in the target, and how many bytes
 *     might already have been written
 *  4. Write the required bytes from trx-binlog to the target binlog
 *  4. Delete summary
 *  5. Delete trx-binlog.
 *
 *  TODO: Decide how to handle recovery failure. It could be an exception telling
 *        the user what to do manually, leaving the recovery files in place (leading
 *        to maxscale oscillating up and down under systemd).
 *        It is also a possibility that the higher level catches the exception,
 *        makes certain checks and then decides if the the target binlog and the
 *        recovery data can be deleted (in that order). This would at least require
 *        that there is a predecessor file and that that file is not compressed
 *        (as of 24.02, decompress if it is). Pinloki will then request and recreate
 *        the entire file. File readers can already handle this situation.
 */
class TrxFile
{
public:
    enum Mode {RECOVER, WRITE};

    TrxFile(InventoryWriter* pInv, Mode mode, const maxsql::Gtid& gtid = maxsql::Gtid {});

    // Writes to trx-binlog
    void add_log_data(const char* pData, int64_t size);

    int64_t size() const;

    // Writes the summary file and calls recover().
    WritePosition& commit(WritePosition& pos, const maxsql::Gtid& gtid);

private:
    WritePosition& recover(WritePosition& pos);

    InventoryWriter& m_inventory;
    Mode             m_mode;
    std::string      m_trx_binlog_filename;
    std::string      m_summary_filename;
    std::ofstream    m_trx_binlog;
    int64_t          m_size = 0;
    maxsql::Gtid     m_gtid;
};

// TODO: Missing error checking in all of TrxFile
TrxFile::TrxFile(InventoryWriter* pInv, Mode mode, const maxsql::Gtid& gtid)
    : m_inventory(*pInv)
    , m_mode(mode)
    , m_trx_binlog_filename(m_inventory.config().trx_dir() + "/trx-binlog")
    , m_summary_filename(m_inventory.config().trx_dir() + "/summary")
    , m_gtid(gtid)
{
    if (mode == RECOVER)
    {
        WritePosition pos;
        recover(pos);
    }
    else
    {
        mxb_assert(!std::ifstream(m_trx_binlog_filename).good());
        mxb_assert(!std::ifstream(m_summary_filename).good());

        // Write the gtid to the summary file for logging
        auto tmp_name = m_summary_filename + ".tmp";
        std::ofstream tmp_summary(tmp_name);
        tmp_summary << gtid;
        tmp_summary.flush();
        rename(tmp_name.c_str(), m_summary_filename.c_str());

        m_trx_binlog.open(m_trx_binlog_filename);
    }
}

void TrxFile::add_log_data(const char* pData, int64_t size)
{
    m_trx_binlog.write(pData, size);
    m_size += size;

    CRASH_TEST(fail_mid_trx);
}

int64_t TrxFile::size() const
{
    return m_size;
}

WritePosition& TrxFile::commit(WritePosition& pos, const maxsql::Gtid& gtid)
{
    m_trx_binlog.close();

    auto tmp_name = m_summary_filename + ".tmp";
    std::ofstream tmp_summary(tmp_name);
    tmp_summary << gtid << ' ' << pos.name << ' ' << pos.write_pos << ' ' << "TRX";
    tmp_summary.flush();
    rename(tmp_name.c_str(), m_summary_filename.c_str());

    CRASH_TEST(startup_recovery_soft);
    CRASH_TEST(startup_recovery_hard);
    CRASH_TEST(fail_after_commit);

    return recover(pos);
}

WritePosition& TrxFile::recover(WritePosition& pos)
{
    std::ifstream summary(m_summary_filename);
    std::string target_name;
    int64_t start_file_pos;
    std::string gtid_str;
    std::string end_marker;
    summary >> gtid_str >> target_name >> start_file_pos >> end_marker;
    m_gtid = maxsql::Gtid::from_string(gtid_str);

    if (end_marker != "TRX")
    {
        std::ifstream trx(m_trx_binlog_filename);
        if (trx)
        {
            MXB_SWARNING("Binlog transaction recovery. Removing temporary transaction files for"
                         " incomplete transaction"
                         << (m_gtid.is_valid() ? " with gtid "s + gtid_str : "") << '.');
        }
        remove_dir_contents(m_inventory.config().trx_dir().c_str());
        return pos;
    }

    if (!pos.file.is_open())
    {
        auto last_file_name = last_string(m_inventory.file_names());
        if (!comp_paths(target_name, last_file_name))
        {
            MXB_SERROR("Binlog transaction recovery. The last binlog file '"
                       << last_file_name << "' is not the expected '" << target_name
                       << "'. Removing temporary transaction files for gtid " << m_gtid << '.');
            remove_dir_contents(m_inventory.config().trx_dir().c_str());
            return pos;
        }

        pos.name = target_name;
        pos.write_pos = start_file_pos;
        pos.file.open(pos.name, std::ios_base::in | std::ios_base::out);
    }

    std::ifstream trx_file(m_trx_binlog_filename);
    trx_file.seekg(0, std::ios_base::end);

    append_file(trx_file, pos.file, start_file_pos);
    pos.write_pos = pos.file.tellp();

    remove_dir_contents(m_inventory.config().trx_dir().c_str());

    if (m_mode == RECOVER)
    {
        MXB_SWARNING("Binlog transaction recovery. Recovered transaction with gtid "
                     << m_gtid << " from temporary transaction files.");
    }

    return pos;
}

void perform_transaction_recovery(InventoryWriter* inv)
{
    // The constructor does recovery
    TrxFile trx_file(inv, TrxFile::RECOVER);
}

/////
///// Transaction implementation
/////

Transaction::Transaction(InventoryWriter* pInv)
    : m_inventory(*pInv)
{
}

Transaction::~Transaction()
{
}

bool Transaction::add_event(maxsql::RplEvent& rpl_event)
{
    if (!m_in_transaction)
    {
        return false;
    }

    const char* ptr = rpl_event.pBuffer();
    m_trx_buffer.insert(m_trx_buffer.end(), ptr, ptr + rpl_event.buffer_size());

    if (m_trx_buffer.size() >= size_t(m_inventory.config().trx_buffer_size()))
    {
        if (!m_trx_file)
        {
            m_trx_file = std::make_unique<TrxFile>(&m_inventory, TrxFile::WRITE, m_gtid);
        }

        m_trx_file->add_log_data(m_trx_buffer.data(), m_trx_buffer.size());
        m_trx_buffer.clear();
        // TODO handle dynamic decrease in config().trx_buffer_size()
    }

    return true;
}

int64_t Transaction::size() const
{
    return m_trx_buffer.size() + (m_trx_file ? m_trx_file->size() : 0);
}

void Transaction::begin(const maxsql::Gtid& gtid)
{
    mxb_assert(m_in_transaction == false);
    m_gtid = gtid;
    m_in_transaction = true;
}

WritePosition& Transaction::commit(WritePosition& pos)
{
    mxb_assert(m_in_transaction == true);

    if (m_trx_file)
    {
        if (!m_trx_buffer.empty())
        {
            m_trx_file->add_log_data(m_trx_buffer.data(), m_trx_buffer.size());
        }

        m_trx_file->commit(pos, m_gtid);
        m_trx_file.reset();
    }
    else
    {
        pos.file.seekp(pos.write_pos);
        pos.file.write(m_trx_buffer.data(), m_trx_buffer.size());

        pos.write_pos = pos.file.tellp();
        pos.file.flush();
    }

    m_trx_buffer.clear();
    m_in_transaction = false;

    return pos;
}
}
