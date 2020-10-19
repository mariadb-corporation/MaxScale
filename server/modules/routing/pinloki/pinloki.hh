/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
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

class Pinloki : public mxs::Router
{
public:
    Pinloki(const Pinloki&) = delete;
    Pinloki& operator=(const Pinloki&) = delete;

    ~Pinloki() = default;
    static Pinloki*     create(SERVICE* pService, mxs::ConfigParameters* pParams);
    mxs::RouterSession* newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints);
    json_t*             diagnostics() const;
    uint64_t            getCapabilities() const;
    bool                configure(mxs::ConfigParameters* pParams);

    mxs::config::Configuration* getConfiguration()
    {
        return &m_config;
    }

    const Config&    config() const;
    InventoryWriter* inventory();

    std::string   change_master(const parser::ChangeMasterValues& values);
    bool          is_slave_running() const;
    std::string   start_slave();
    void          stop_slave();
    void          reset_slave();
    GWBUF*        show_slave_status(bool all) const;
    mxq::GtidList gtid_io_pos() const;
    void          set_gtid(const mxq::GtidList& gtid);

private:
    Pinloki(SERVICE* pService, Config&& config);

    maxsql::Connection::ConnectionDetails generate_details();

    bool        purge_old_binlogs(maxbase::Worker::Call::action_t action);
    std::string verify_master_settings();

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
        bool        ssl_verify_server_cert = false;

        void save(const Config& config) const;
        bool load(const Config& config);
    };

    Config                  m_config;
    SERVICE*                m_service;
    InventoryWriter         m_inventory;
    std::unique_ptr<Writer> m_writer;
    MasterConfig            m_master_config;
    mutable std::mutex      m_lock;
};

std::pair<std::string, std::string> get_file_name_and_size(const std::string& filepath);

/**
 * @brief PurgeResult enum
 *        Ok               - Files deleted
 *        UpToFileNotFound - The file "up_to" was not found
 *        PartialPurge     - File purge stopped because a file to be purged was in use
 */
enum class PurgeResult {Ok, UpToFileNotFound, PartialPurge};

PurgeResult purge_binlogs(InventoryWriter* pInventory, const std::string& up_to);
}
