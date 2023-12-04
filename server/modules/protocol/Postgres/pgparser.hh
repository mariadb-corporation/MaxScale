/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/cachingparser.hh>


class PgParser : public maxscale::CachingParser
{
public:
    class Helper : public mxs::Parser::Helper
    {
    public:
        static const Helper& get();

        GWBUF create_packet(std::string_view sql) const override;

        const char*      client_command_to_string(uint32_t cmd) const override;
        bool             command_will_respond(uint32_t cmd) const override;
        bool             continues_ps(const GWBUF& packet, uint32_t prev_cmd) const override;
        uint32_t         get_command(const GWBUF& packet) const override;
        PacketTypeMask   get_packet_type_mask(const GWBUF& packet) const override;
        uint32_t         get_ps_id(const GWBUF& packet) const override;
        std::string_view get_sql(const GWBUF& packet) const override;
        bool             is_empty(const GWBUF& packet) const override;
        bool             is_execute_immediately_ps(uint32_t id) const override;
        bool             is_multi_part_packet(const GWBUF& packet) const override;
        bool             is_prepare(const GWBUF& packet) const override;
        bool             is_ps_direct_exec_id(uint32_t id) const override;
        bool             is_ps_packet(const GWBUF& packet) const override;
        bool             is_query(const GWBUF& packet) const override;
        QueryInfo        get_query_info(const GWBUF& packet) const override;
    };

    PgParser(const PgParser&) = delete;
    PgParser& operator=(const PgParser&) = delete;

    PgParser(std::unique_ptr<mxs::Parser> sParser);
    ~PgParser();
};
