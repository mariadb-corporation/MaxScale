/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2023-11-05
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pinloki.hh"
#include "pinlokisession.hh"

#include <maxscale/protocol/mariadb/resultset.hh>
#include <maxscale/json.hh>
#include <fstream>

namespace pinloki
{

Pinloki::Pinloki(SERVICE* pService, Config&& config)
    : Router<Pinloki, PinlokiSession>(pService)
    , m_config(std::move(config))
    , m_inventory(m_config)
{
    if (auto ifs = std::ifstream(m_config.gtid_file_path()))
    {
        std::string gtid_list_str;
        ifs >> gtid_list_str;
        m_config.set_boot_strap_gtid_list(gtid_list_str);
    }

    if (m_master_config.load(m_config) && m_master_config.slave_running)
    {
        start_slave();
    }
}
// static
Pinloki* Pinloki::create(SERVICE* pService, mxs::ConfigParameters* pParams)
{
    Pinloki* rval = nullptr;
    Config config(pService->name());

    if (config.configure(*pParams))
    {
        rval = new Pinloki(pService, std::move(config));
    }

    return rval;
}

PinlokiSession* Pinloki::newSession(MXS_SESSION* pSession, const Endpoints& endpoints)
{
    return new PinlokiSession(pSession, this);
}

json_t* Pinloki::diagnostics() const
{
    return nullptr;
}

uint64_t Pinloki::getCapabilities()
{
    return RCAP_TYPE_CONTIGUOUS_INPUT;
}

bool Pinloki::configure(mxs::ConfigParameters* pParams)
{
    return true;
}

const Config& Pinloki::config() const
{
    return m_config;
}

Inventory* Pinloki::inventory()
{
    return &m_inventory;
}

void Pinloki::change_master(const parser::ChangeMasterValues& values)
{
    std::lock_guard<std::mutex> guard(m_lock);

    using CMT = pinloki::ChangeMasterType;

    for (const auto& a : values)
    {
        switch (a.first)
        {
        case CMT::MASTER_HOST:
            m_master_config.host = a.second;
            break;

        case CMT::MASTER_PORT:
            m_master_config.port = atoi(a.second.c_str());
            break;

        case CMT::MASTER_USER:
            m_master_config.user = a.second;
            break;

        case CMT::MASTER_PASSWORD:
            m_master_config.password = a.second;
            break;

        case CMT::MASTER_USE_GTID:
            m_master_config.use_gtid = strcasecmp(a.second.c_str(), "slave_pos") == 0;
            break;

        case CMT::MASTER_SSL:
            m_master_config.ssl = a.second.front() != '0';
            break;

        case CMT::MASTER_SSL_CA:
            m_master_config.ssl_ca = a.second;
            break;

        case CMT::MASTER_SSL_CAPATH:
            m_master_config.ssl_capath = a.second;
            break;

        case CMT::MASTER_SSL_CERT:
            m_master_config.ssl_cert = a.second;
            break;

        case CMT::MASTER_SSL_CRL:
            m_master_config.ssl_crl = a.second;
            break;

        case CMT::MASTER_SSL_CRLPATH:
            m_master_config.ssl_crlpath = a.second;
            break;

        case CMT::MASTER_SSL_KEY:
            m_master_config.ssl_key = a.second;
            break;

        case CMT::MASTER_SSL_CIPHER:
            m_master_config.ssl_cipher = a.second;
            break;

        case CMT::MASTER_SSL_VERIFY_SERVER_CERT:
            m_master_config.ssl_verify_server_cert = a.second.front() != '0';
            break;
        }
    }

    m_master_config.save(m_config);
}

bool Pinloki::is_slave_running() const
{
    std::lock_guard<std::mutex> guard(m_lock);
    return m_writer.get();
}

bool Pinloki::start_slave()
{
    bool rval = false;
    std::lock_guard<std::mutex> guard(m_lock);
    const auto& cfg = m_master_config;

    if (!cfg.host.empty() && cfg.port && !cfg.user.empty() && !cfg.password.empty())
    {
        MXS_INFO("Starting slave");

        maxsql::Connection::ConnectionDetails details;
        details.host = mxb::Host(m_master_config.host, m_master_config.port);
        details.user = m_master_config.user;
        details.password = m_master_config.password;
        details.timeout = m_config.net_timeout();

        m_writer = std::make_unique<Writer>(details, inventory());
        rval = true;

        m_master_config.slave_running = true;
        m_master_config.save(m_config);
    }

    return rval;
}

void Pinloki::stop_slave()
{
    std::lock_guard<std::mutex> guard(m_lock);
    MXS_INFO("Stopping slave");
    m_writer.reset();
    m_master_config.slave_running = false;
    m_master_config.save(m_config);
}

void Pinloki::reset_slave()
{
    std::lock_guard<std::mutex> guard(m_lock);
    MXS_INFO("Resetting slave");
    m_master_config = MasterConfig();
}

GWBUF* Pinloki::show_slave_status() const
{
    std::lock_guard<std::mutex> guard(m_lock);

    auto rset = ResultSet::create({});
    rset->add_row({});

    rset->add_column("Slave_IO_State", m_writer ? "Waiting for master to send event" : "");
    rset->add_column("Master_Host", m_master_config.host);
    rset->add_column("Master_User", m_master_config.user);
    rset->add_column("Master_Port", std::to_string(m_master_config.port));
    rset->add_column("Connect_Retry", "1");
    rset->add_column("Master_Log_File", "TODO: Read from writer");
    rset->add_column("Read_Master_Log_Pos", "TODO: Read from writer");
    rset->add_column("Relay_Log_File", "mysqld-relay-bin.000001");
    rset->add_column("Relay_Log_Pos", "4");
    rset->add_column("Relay_Master_Log_File", "binlog.000001");
    rset->add_column("Slave_IO_Running", m_writer ? "Yes" : "No");
    rset->add_column("Slave_SQL_Running", m_writer ? "Yes" : "No");
    rset->add_column("Replicate_Do_DB", "");
    rset->add_column("Replicate_Ignore_DB", "");
    rset->add_column("Replicate_Do_Table", "");
    rset->add_column("Replicate_Ignore_Table", "");
    rset->add_column("Replicate_Wild_Do_Table", "");
    rset->add_column("Replicate_Wild_Ignore_Table", "");
    rset->add_column("Last_Errno", "0");
    rset->add_column("Last_Error", "");
    rset->add_column("Skip_Counter", "0");
    rset->add_column("Exec_Master_Log_Pos", "TODO: Read from writer");
    rset->add_column("Relay_Log_Space", "0");
    rset->add_column("Until_Condition", "None");
    rset->add_column("Until_Log_File", "");
    rset->add_column("Until_Log_Pos", "0");
    rset->add_column("Master_SSL_Allowed", "No");
    rset->add_column("Master_SSL_CA_File", "");
    rset->add_column("Master_SSL_CA_Path", "");
    rset->add_column("Master_SSL_Cert", "");
    rset->add_column("Master_SSL_Cipher", "");
    rset->add_column("Master_SSL_Key", "");
    rset->add_column("Seconds_Behind_Master", "0");
    rset->add_column("Master_SSL_Verify_Server_Cert", "No");
    rset->add_column("Last_IO_Errno", "0");
    rset->add_column("Last_IO_Error", "");
    rset->add_column("Last_SQL_Errno", "0");
    rset->add_column("Last_SQL_Error", "");
    rset->add_column("Replicate_Ignore_Server_Ids", "");
    rset->add_column("Master_Server_Id", "1");
    rset->add_column("Master_SSL_Crl", "");
    rset->add_column("Master_SSL_Crlpath", "");
    rset->add_column("Using_Gtid", "Slave_Pos");
    rset->add_column("Gtid_IO_Pos", "0-1-1");
    rset->add_column("Replicate_Do_Domain_Ids", "");
    rset->add_column("Replicate_Ignore_Domain_Ids", "");
    rset->add_column("Parallel_Mode", "conservative");
    rset->add_column("SQL_Delay", "0");
    rset->add_column("SQL_Remaining_Delay", "NULL");
    rset->add_column("Slave_SQL_Running_State",
                     "Slave has read all relay log; waiting for the slave I/O thread to update it");
    rset->add_column("Slave_DDL_Groups", "0");
    rset->add_column("Slave_Non_Transactional_Groups", "0");
    rset->add_column("Slave_Transactional_Groups", "0");

    return rset->as_buffer().release();
}

void Pinloki::set_gtid(const mxq::GtidList& gtid)
{
    m_config.set_boot_strap_gtid_list(gtid.to_string());
}

void Pinloki::MasterConfig::save(const Config& config) const
{
    auto js = json_pack(
        "{"
        "s: b,"     // slave_running
        "s: s,"     // host
        "s: i,"     // port
        "s: s,"     // user
        "s: s,"     // password
        "s: b,"     // use_gtid
        "s: b,"     // ssl
        "s: s,"     // ssl_ca
        "s: s,"     // ssl_capath
        "s: s,"     // ssl_cert
        "s: s,"     // ssl_crl
        "s: s,"     // ssl_crlpath
        "s: s,"     // ssl_key
        "s: s,"     // ssl_cipher
        "s: b"      // ssl_verify_server_cert
        "}",
        "slave_running", slave_running,
        "host", host.c_str(),
        "port", port,
        "user", user.c_str(),
        "password", password.c_str(),   // TODO: Encrypt this
        "use_gtid", use_gtid,
        "ssl", ssl,
        "ssl_ca", ssl_ca.c_str(),
        "ssl_capath", ssl_capath.c_str(),
        "ssl_cert", ssl_cert.c_str(),
        "ssl_crl", ssl_crl.c_str(),
        "ssl_crlpath", ssl_crlpath.c_str(),
        "ssl_key", ssl_key.c_str(),
        "ssl_cipher", ssl_cipher.c_str(),
        "ssl_verify_server_cert", ssl_verify_server_cert);

    mxb_assert(js);
    json_dump_file(js, config.master_info_file().c_str(), JSON_COMPACT);
    json_decref(js);
}

bool Pinloki::MasterConfig::load(const Config& config)
{
    bool rval = false;

    if (access(config.master_info_file().c_str(), F_OK) == 0)
    {
        json_error_t err;
        auto js = json_load_file(config.master_info_file().c_str(), 0, &err);

        if (js)
        {
            rval = true;

            mxs::get_json_bool(js, "slave_running", &slave_running);
            mxs::get_json_string(js, "host", &host);
            mxs::get_json_int(js, "port", &port);
            mxs::get_json_string(js, "user", &user);
            mxs::get_json_string(js, "password", &password);
            mxs::get_json_bool(js, "use_gtid", &use_gtid);
            mxs::get_json_bool(js, "ssl", &ssl);
            mxs::get_json_string(js, "ssl_ca", &ssl_ca);
            mxs::get_json_string(js, "ssl_capath", &ssl_capath);
            mxs::get_json_string(js, "ssl_cert", &ssl_cert);
            mxs::get_json_string(js, "ssl_crl", &ssl_crl);
            mxs::get_json_string(js, "ssl_crlpath", &ssl_crlpath);
            mxs::get_json_string(js, "ssl_key", &ssl_key);
            mxs::get_json_string(js, "ssl_cipher", &ssl_cipher);
            mxs::get_json_bool(js, "ssl_verify_server_cert", &ssl_verify_server_cert);
        }
        else
        {
            MXS_INFO("Failed to load master info JSON file: %s", err.text);
        }
    }

    return rval;
}
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        MXS_MODULE_API_ROUTER,
        MXS_MODULE_ALPHA_RELEASE,
        MXS_ROUTER_VERSION,
        "Pinloki",
        "V1.0.0",
        RCAP_TYPE_CONTIGUOUS_INPUT,
        &pinloki::Pinloki::s_object,
        NULL,
        NULL,
        NULL,
        NULL
    };

    pinloki::Config::spec().populate(info);

    return &info;
}
