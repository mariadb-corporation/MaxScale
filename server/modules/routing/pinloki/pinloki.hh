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

#include <array>
#include <mutex>
#include <string>

#include <maxbase/exception.hh>
#include <maxscale/router.hh>

#include "writer.hh"
#include "config.hh"
#include "parser.hh"

namespace pinloki
{
DEFINE_EXCEPTION(BinlogReadError);
DEFINE_EXCEPTION(GtidNotFoundError);

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

    const Config& config() const;
    Inventory*    inventory();

    void   change_master(const parser::ChangeMasterValues& values);
    bool   is_slave_running() const;
    bool   start_slave();
    void   stop_slave();
    void   reset_slave();
    GWBUF* show_slave_status() const;
    void   set_gtid(const mxq::GtidList& gtid);

private:
    Pinloki(SERVICE* pService, Config&& config);

    maxsql::Connection::ConnectionDetails generate_details();

    struct MasterConfig
    {
        bool        slave_running = false;
        std::string host;
        int64_t     port = 0;
        std::string user;
        std::string password;
        bool        use_gtid = false;

        bool        ssl = false;
        std::string ssl_ca;
        std::string ssl_capath;
        std::string ssl_cert;
        std::string ssl_crl;
        std::string ssl_crlpath;
        std::string ssl_key;
        std::string ssl_cipher;
        bool        ssl_verify_server_cert;

        void save(const Config& config) const;
        bool load(const Config& config);
    };

    Config                  m_config;
    Inventory               m_inventory;
    std::unique_ptr<Writer> m_writer;
    MasterConfig            m_master_config;
    mutable std::mutex      m_lock;
};
}
