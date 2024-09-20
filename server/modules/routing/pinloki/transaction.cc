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
}

namespace pinloki
{

bool Transaction::add_event(maxsql::RplEvent& rpl_event)
{
    if (!m_in_transaction)
    {
        return false;
    }

    const char* ptr = rpl_event.pBuffer();
    m_trx_buffer.insert(m_trx_buffer.end(), ptr, ptr + rpl_event.buffer_size());

    return true;
}

int64_t Transaction::size() const
{
    return m_trx_buffer.size();
}

void Transaction::begin(const maxsql::Gtid& gtid)
{
    mxb_assert(m_in_transaction == false);

    m_in_transaction = true;
}

WritePosition& Transaction::commit(WritePosition& pos)
{
    mxb_assert(m_in_transaction == true);

    pos.file.seekp(pos.write_pos);
    pos.file.write(m_trx_buffer.data(), m_trx_buffer.size());

    pos.write_pos = pos.file.tellp();
    pos.file.flush();

    m_in_transaction = false;
    m_trx_buffer.clear();

    return pos;
}
}
