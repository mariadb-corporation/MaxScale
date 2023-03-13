/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "postgresprotocol.hh"
#include <maxscale/session.hh>

class PgProtocolData final : public MXS_SESSION::ProtocolData
{
public:
    ~PgProtocolData();

    bool will_respond(const GWBUF& buffer) const override;
    bool can_recover_state() const override;
    bool is_trx_starting() const override;
    bool is_trx_active() const override;
    bool is_trx_read_only() const override;
    bool is_trx_ending() const override;

    size_t amend_memory_statistics(json_t* memory) const override;
    size_t static_size() const override;
    size_t varying_size() const override;

    void set_connect_params(const GWBUF& buf)
    {
        mxb_assert_message(buf.length() > 8, "StartupMessage should be longer than 8 bytes");
        m_params.assign(buf.data() + 8, buf.end());
    }

    const std::vector<uint8_t>& connect_params() const
    {
        return m_params;
    }

private:
    std::vector<uint8_t> m_params;
};
