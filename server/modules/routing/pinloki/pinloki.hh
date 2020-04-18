/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-03-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#pragma once

#include <maxscale/ccdefs.hh>

#include <string>
#include <array>
#include <maxbase/exception.hh>
#include <maxscale/router.hh>

#include "writer.hh"
#include "config.hh"

namespace pinloki
{
DEFINE_EXCEPTION(BinlogReadError);

static std::array<char, 4> PINLOKI_MAGIC = {char(0xfe), 0x62, 0x69, 0x6e};

struct FileLocation
{
    std::string file_name;
    long        loc;
};

class PinlokiSession;

class Pinloki : public mxs::Router<Pinloki, PinlokiSession>
{
public:
    Pinloki(const Pinloki&) = delete;
    Pinloki& operator=(const Pinloki&) = delete;

    ~Pinloki() = default;
    static Pinloki* create(SERVICE* pService, mxs::ConfigParameters* pParams);
    PinlokiSession* newSession(MXS_SESSION* pSession, const Endpoints& endpoints);
    json_t*         diagnostics() const;
    uint64_t        getCapabilities();
    bool            configure(mxs::ConfigParameters* pParams);

    const Config& config() const
    {
        return m_config;
    }

    Inventory* inventory()
    {
        return &m_inventory;
    }

private:
    Pinloki(SERVICE* pService);

    Config    m_config;
    Inventory m_inventory;
};
}
