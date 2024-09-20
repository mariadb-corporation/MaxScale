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
#pragma once

#include "rpl_event.hh"
#include "inventory.hh"

#include <string>
#include <fstream>

namespace pinloki
{

struct WritePosition
{
    std::string   name;
    std::ofstream file;
    int64_t       write_pos;
};

class TrxFile;

class Transaction
{
public:
    Transaction(InventoryWriter* pInv);
    ~Transaction();

    void           begin(const maxsql::Gtid& gtid);
    bool           add_event(maxsql::RplEvent& rpl_event);
    int64_t        size() const;
    WritePosition& commit(WritePosition& pos);
private:
    InventoryWriter&  m_inventory;
    std::vector<char> m_trx_buffer;
    bool              m_in_transaction = false;

    std::unique_ptr<TrxFile> m_trx_file;
};

void perform_transaction_recovery(InventoryWriter* pInv);
}
