/*
 * Copyright (c) 2023 MariaDB plc
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
#pragma once

#include "postgresprotocol.hh"
#include <maxscale/config2.hh>

class PgProtocolModule;

class PgConfiguration final : public mxs::config::Configuration
{
public:
    PgConfiguration(const std::string& name, PgProtocolModule* pInstance);

    // static
    static mxs::config::Specification& specification();

    std::string parser;

    static constexpr const char* MARIADB = "mariadb";
    static constexpr const char* PP_PG_QUERY = "pp_pg_query";

    bool post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params) override final;

private:
    PgProtocolModule& m_instance;
};
