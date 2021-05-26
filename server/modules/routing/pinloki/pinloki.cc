/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "pinloki.hh"
#include "pinlokisession.hh"

#include <maxscale/protocol/mariadb/resultset.hh>
#include <maxscale/json.hh>
#include <maxscale/modutil.hh>

#include <fstream>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>

using namespace std::chrono_literals;


using maxbase::operator<<;
using wall_time::operator<<;

namespace pinloki
{

namespace
{

/**
 * @brief get_inode
 * @param file_name to check. Links are followed.
 * @return the inode of the file, or a negative number if something went wrong
 */
int get_inode(const std::string& file_name)
{
    int fd = open(file_name.c_str(), O_RDONLY);

    if (fd < 0)
    {
        return -1;
    }

    struct stat file_stat;
    int ret = fstat(fd, &file_stat);
    if (ret < 0)
    {
        close(fd);
        return -1;
    }

    close(fd);
    return file_stat.st_ino;
}

/**
 * @brief get_open_inodes
 * @return return a vector of inodes of the files the program currently has open
 */
std::vector<int> get_open_inodes()
{
    std::vector<int> vec;
    const std::string proc_fd_dir = "/proc/self/fd";

    DIR* dir;
    struct dirent* ent;
    if ((dir = opendir(proc_fd_dir.c_str())) != nullptr)
    {
        while ((ent = readdir (dir)) != nullptr)
        {
            if (ent->d_type == DT_LNK)
            {
                int inode = get_inode(proc_fd_dir + '/' + ent->d_name);
                if (inode >= 0)
                {
                    vec.push_back(inode);
                }
            }
        }
        closedir (dir);
    }
    else
    {
        MXB_SERROR("Could not open directory " << proc_fd_dir);
        mxb_assert(!true);
    }

    return vec;
}

/** Last modification time of file_name, or wall_time::TimePoint::max() on error */
wall_time::TimePoint file_mod_time(const std::string& file_name)
{
    auto ret = wall_time::TimePoint::max();
    int fd = open(file_name.c_str(), O_RDONLY);
    if (fd >= 0)
    {
        struct stat file_stat;
        if (fstat(fd, &file_stat) >= 0)
        {
            ret = mxb::timespec_to_time_point<wall_time::Clock>(file_stat.st_mtim);
        }
        close(fd);
    }

    return ret;
}

/** Modification time of the oldest log file or wall_time::TimePoint::max() if there are no logs */
wall_time::TimePoint oldest_logfile_time(InventoryWriter* pInventory)
{
    auto ret = wall_time::TimePoint::max();
    const auto& file_names = pInventory->file_names();
    if (!file_names.empty())
    {
        ret = file_mod_time(first_string(file_names));
    }

    return ret;
}
}


std::pair<std::string, std::string> get_file_name_and_size(const std::string& filepath)
{
    std::string file = filepath;
    std::string size = "0";

    if (!file.empty())
    {
        auto pos = file.find_last_of('/');

        if (pos != std::string::npos)
        {
            file = file.substr(pos + 1);
        }

        struct stat st;
        if (stat(filepath.c_str(), &st) == 0)
        {
            size = std::to_string(st.st_size);
        }
    }

    return {file, size};
}

Pinloki::Pinloki(SERVICE* pService)
    : m_config(pService->name(), [this]() {
                   return post_configure();
               }),
    m_service(pService),
    m_inventory(m_config)
{
}

bool Pinloki::post_configure()
{
    m_inventory.configure();

    if (m_master_config.load(m_config))
    {
        if (m_master_config.slave_running)
        {
            start_slave();
        }
    }
    else if (m_config.select_master())
    {
        start_slave();
    }

    // Kick off the independent purging
    if (m_config.expire_log_duration().count())
    {
        maxbase::Worker* worker = maxbase::Worker::get_current();
        mxb_assert(worker);

        using namespace std::chrono;
        auto ms = duration_cast<milliseconds>(m_config.purge_startup_delay());
        worker->delayed_call(ms, &Pinloki::purge_old_binlogs, this);
    }

    return true;
}

// static
Pinloki* Pinloki::create(SERVICE* pService)
{
    pService->set_custom_version_suffix("-BinlogRouter");
    return new Pinloki(pService);
}

mxs::RouterSession* Pinloki::newSession(MXS_SESSION* pSession, const mxs::Endpoints& endpoints)
{
    return new PinlokiSession(pSession, this);
}

json_t* Pinloki::diagnostics() const
{
    json_t* rval = json_object();
    std::lock_guard<std::mutex> guard(m_lock);

    auto current_binlog = last_string(m_inventory.file_names());

    json_object_set_new(rval, "gtid_io_pos", json_string(gtid_io_pos().to_string().c_str()));
    json_object_set_new(rval, "current_binlog", json_string(current_binlog.c_str()));

    json_t* cnf = json_object();
    json_object_set_new(cnf, "host", json_string(m_master_config.host.c_str()));
    json_object_set_new(cnf, "port", json_integer(m_master_config.port));
    json_object_set_new(cnf, "user", json_string(m_master_config.user.c_str()));
    json_object_set_new(cnf, "ssl", json_boolean(m_master_config.ssl));

    if (m_master_config.ssl)
    {
        json_object_set_new(cnf, "ssl_ca", json_string(m_master_config.ssl_ca.c_str()));
        json_object_set_new(cnf, "ssl_capath", json_string(m_master_config.ssl_capath.c_str()));
        json_object_set_new(cnf, "ssl_cert", json_string(m_master_config.ssl_cert.c_str()));
        json_object_set_new(cnf, "ssl_cipher", json_string(m_master_config.ssl_cipher.c_str()));
        json_object_set_new(cnf, "ssl_crl", json_string(m_master_config.ssl_crl.c_str()));
        json_object_set_new(cnf, "ssl_crlpath", json_string(m_master_config.ssl_crlpath.c_str()));
        json_object_set_new(cnf, "ssl_key", json_string(m_master_config.ssl_key.c_str()));
        json_object_set_new(cnf, "ssl_verify_server_cert",
                            json_boolean(m_master_config.ssl_verify_server_cert));
    }

    json_object_set_new(rval, "master_config", cnf);

    return rval;
}

uint64_t Pinloki::getCapabilities() const
{
    return RCAP_TYPE_STMT_INPUT;
}

const Config& Pinloki::config() const
{
    return m_config;
}

InventoryWriter* Pinloki::inventory()
{
    return &m_inventory;
}

std::string Pinloki::change_master(const parser::ChangeMasterValues& values)
{
    std::lock_guard<std::mutex> guard(m_lock);

    if (m_config.select_master())
    {
        MXB_SINFO("Turning off select_master functionality"
                  " due to 'CHANGE MASTER TO' command. select_master"
                  " will take effect again in the next MaxScale restart.");
    }

    m_config.disable_select_master();

    using CMT = pinloki::ChangeMasterType;
    std::vector<std::string> errors;

    for (const auto& a : values)
    {
        switch (a.first)
        {
        case CMT::MASTER_HOST:
            m_master_config.host = a.second;
            break;

        case CMT::MASTER_PORT:
            m_master_config.port = atoi(a.second.c_str());
            if (m_master_config.port == 0)
            {
                errors.push_back(MAKE_STR("Invalid port number " << a.second));
            }
            break;

        case CMT::MASTER_USER:
            m_master_config.user = a.second;
            break;

        case CMT::MASTER_PASSWORD:
            m_master_config.password = a.second;
            break;

        case CMT::MASTER_USE_GTID:
            // slave_pos or current_pos, does not matter which
            m_master_config.use_gtid = strcasecmp(a.second.c_str(), "slave_pos") == 0
                || strcasecmp(a.second.c_str(), "current_pos") == 0;

            if (!m_master_config.use_gtid)
            {
                errors.push_back("MASTER_USE_GTID must specify slave_pos or current_pos");
            }
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

        case CMT::MASTER_LOG_FILE:
        case CMT::MASTER_LOG_POS:
        case CMT::RELAY_LOG_FILE:
        case CMT::RELAY_LOG_POS:
            errors.push_back("Binlogrouter does not support file/position based replication."
                             " Use MASTER_USE_GTID=slave_pos.");
            break;

        case CMT::MASTER_HEARTBEAT_PERIOD:
            MXB_SWARNING("Option " << to_string(a.first) << " ignored");
            break;

        default:
            errors.push_back("Binlogrouter does not yet support the option " + to_string(a.first));
            break;
        }
    }

    std::string err_str = mxb::join(errors, "\n");

    if (err_str.empty())
    {
        m_master_config.save(m_config);
    }

    return err_str;
}

std::string Pinloki::verify_master_settings()
{
    if (m_config.select_master())
    {
        return "";
    }

    using CMT = pinloki::ChangeMasterType;
    std::set<CMT> mandatory {CMT::MASTER_HOST, CMT::MASTER_PORT, CMT::MASTER_USER,
                             CMT::MASTER_PASSWORD, CMT::MASTER_USE_GTID};
    auto mandatory_notset {mandatory};
    std::vector<std::string> errors;

    for (const auto& m : mandatory)
    {
        bool erase = false;

        switch (m)
        {
        case CMT::MASTER_HOST:
            if (!m_master_config.host.empty())
            {
                erase = true;
            }
            break;

        case CMT::MASTER_PORT:
            if (m_master_config.port != 0)
            {
                erase = true;
            }
            break;

        case CMT::MASTER_USER:
            if (!m_master_config.user.empty())
            {
                erase = true;
            }
            break;

        case CMT::MASTER_PASSWORD:
            if (!m_master_config.password.empty())
            {
                erase = true;
            }
            break;

        case CMT::MASTER_USE_GTID:
            if (m_master_config.use_gtid)
            {
                erase = true;
            }
            break;

        default:
            break;
        }

        if (erase)
        {
            mandatory_notset.erase(m);
        }
    }

    for (auto& v : mandatory_notset)
    {
        errors.push_back(MAKE_STR("Mandatory value " << to_string(v) << " not provided"));
    }

    std::string err_str = mxb::join(errors, "\n");

    if (!err_str.empty())
    {
        MXS_SERROR(err_str);
    }

    return err_str;
}

bool Pinloki::is_slave_running() const
{
    std::lock_guard<std::mutex> guard(m_lock);
    return m_writer.get();
}

maxsql::Connection::ConnectionDetails Pinloki::generate_details()
{
    maxsql::Connection::ConnectionDetails details;
    details.timeout = m_config.net_timeout();

    if (m_config.select_master())
    {
        for (auto srv : m_service->reachable_servers())
        {
            if (srv->is_master())
            {
                details.host = mxb::Host(srv->address(), srv->port());
                m_master_config.host = srv->address();
                m_master_config.port = srv->port();
                details.user = m_master_config.user = m_service->config()->user;
                details.password = m_master_config.password = m_service->config()->password;
                auto ssl = srv->ssl_config();

                if (ssl.enabled)
                {
                    details.ssl = m_master_config.ssl = true;
                    details.ssl_ca = m_master_config.ssl_ca = ssl.ca;
                    details.ssl_cert = m_master_config.ssl_cert = ssl.cert;
                    details.ssl_crl = m_master_config.ssl_crl = ssl.crl;
                    details.ssl_key = m_master_config.ssl_key = ssl.key;
                    details.ssl_cipher = m_master_config.ssl_cipher = ssl.cipher;
                    details.ssl_verify_server_cert =
                        m_master_config.ssl_verify_server_cert = ssl.verify_peer;
                }

                m_master_config.use_gtid = true;
                m_master_config.save(m_config);

                break;
            }
        }
    }
    else
    {
        details.host = mxb::Host(m_master_config.host, m_master_config.port);
        details.user = m_master_config.user;
        details.password = m_master_config.password;

        if (m_master_config.ssl)
        {
            details.ssl = true;
            details.ssl_ca = m_master_config.ssl_ca;
            details.ssl_capath = m_master_config.ssl_capath;
            details.ssl_cert = m_master_config.ssl_cert;
            details.ssl_crl = m_master_config.ssl_crl;
            details.ssl_crlpath = m_master_config.ssl_crlpath;
            details.ssl_key = m_master_config.ssl_key;
            details.ssl_cipher = m_master_config.ssl_cipher;
            details.ssl_verify_server_cert = m_master_config.ssl_verify_server_cert;
        }
    }

    return details;
}

std::string Pinloki::start_slave()
{
    std::lock_guard<std::mutex> guard(m_lock);
    std::string err_str;

    if (m_writer)
    {
        MXS_WARNING("START SLAVE: Slave is already running");
        // TODO, a server would generate a warning, code 1254.
    }
    else
    {
        const auto& cfg = m_master_config;

        std::string err_str = verify_master_settings();

        if (err_str.empty())
        {
            MXS_INFO("Starting slave");

            Writer::Generator generator = std::bind(&Pinloki::generate_details, this);
            m_writer = std::make_unique<Writer>(generator, mxs::MainWorker::get(), inventory());

            m_master_config.slave_running = true;
            m_master_config.save(m_config);
        }
    }

    return err_str;
}

void Pinloki::stop_slave()
{
    std::lock_guard<std::mutex> guard(m_lock);
    MXS_INFO("Stopping slave");

    mxb_assert(m_writer);

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

GWBUF* Pinloki::show_slave_status(bool all) const
{
    std::lock_guard<std::mutex> guard(m_lock);

    const auto& files = m_inventory.file_names();
    auto file_and_pos = get_file_name_and_size(last_string(files));

    auto rset = ResultSet::create({});
    rset->add_row({});

    auto error = m_writer ? m_writer->get_err() : Error {};

    enum class State {Stopped, Connected, Error};

    State state = State::Error;
    if (m_inventory.is_writer_connected())
    {
        state = State::Connected;
    }
    else if (error.code == 0)
    {
        state = State::Stopped;
    }

    std::string sql_state =
        state == State::Stopped ? "" :
        "Slave has read all relay log; waiting for the slave I/O thread to update it";

    std::string sql_io_state =
        state == State::Stopped ? "" :
        state == State::Connected ? "Waiting for master to send event" :
        "Reconnecting after a failed master event read";

    if (all)
    {
        rset->add_column("Connection_name", "");
        rset->add_column("Slave_SQL_State", sql_state);
    }
    rset->add_column("Slave_IO_State", sql_io_state);
    rset->add_column("Master_Host", m_master_config.host);
    rset->add_column("Master_User", m_master_config.user);
    rset->add_column("Master_Port", std::to_string(m_master_config.port));
    rset->add_column("Connect_Retry", "1");
    rset->add_column("Master_Log_File", file_and_pos.first.c_str());
    rset->add_column("Read_Master_Log_Pos", file_and_pos.second.c_str());
    rset->add_column("Relay_Log_File", "");
    rset->add_column("Relay_Log_Pos", "");
    rset->add_column("Relay_Master_Log_File", "");
    rset->add_column("Slave_IO_Running",
                     state == State::Stopped ? "No" :
                     state == State::Connected ? "Yes" :
                     "Connecting");
    rset->add_column("Slave_SQL_Running",
                     state == State::Stopped ? "No" : "Yes");
    rset->add_column("Replicate_Do_DB", "");
    rset->add_column("Replicate_Ignore_DB", "");
    rset->add_column("Replicate_Do_Table", "");
    rset->add_column("Replicate_Ignore_Table", "");
    rset->add_column("Replicate_Wild_Do_Table", "");
    rset->add_column("Replicate_Wild_Ignore_Table", "");
    rset->add_column("Last_Errno", std::to_string(error.code));
    rset->add_column("Last_Error", error.str);
    rset->add_column("Skip_Counter", "0");
    rset->add_column("Exec_Master_Log_Pos", file_and_pos.second.c_str());
    rset->add_column("Relay_Log_Space", "0");
    rset->add_column("Until_Condition", "None");
    rset->add_column("Until_Log_File", "");
    rset->add_column("Until_Log_Pos", "0");
    rset->add_column("Master_SSL_Allowed", "No");   // TODO
    rset->add_column("Master_SSL_CA_File", "");
    rset->add_column("Master_SSL_CA_Path", "");
    rset->add_column("Master_SSL_Cert", "");
    rset->add_column("Master_SSL_Cipher", "");
    rset->add_column("Master_SSL_Key", "");
    // Should set Seconds_Behind_Master to null if state != State::Connected,
    // but that is not (yet) supported by ResultSet.
    rset->add_column("Seconds_Behind_Master", "0");
    rset->add_column("Master_SSL_Verify_Server_Cert", "No");
    rset->add_column("Last_IO_Errno", "0");
    rset->add_column("Last_IO_Error", "");
    rset->add_column("Last_SQL_Errno", "0");
    rset->add_column("Last_SQL_Error", "");
    rset->add_column("Replicate_Ignore_Server_Ids", "");
    rset->add_column("Master_Server_Id", std::to_string(m_inventory.master_id()));
    rset->add_column("Master_SSL_Crl", "");
    rset->add_column("Master_SSL_Crlpath", "");
    rset->add_column("Using_Gtid", "Slave_Pos");
    rset->add_column("Gtid_IO_Pos", gtid_io_pos().to_string());
    rset->add_column("Replicate_Do_Domain_Ids", "");
    rset->add_column("Replicate_Ignore_Domain_Ids", "");
    rset->add_column("Parallel_Mode", "conservative");
    rset->add_column("SQL_Delay", "0");
    rset->add_column("SQL_Remaining_Delay", "NULL");
    rset->add_column("Slave_SQL_Running_State", sql_state);
    rset->add_column("Slave_DDL_Groups", "0");
    rset->add_column("Slave_Non_Transactional_Groups", "0");
    rset->add_column("Slave_Transactional_Groups", "0");

    if (all)
    {
        rset->add_column("Retried_transactions", "0");
        rset->add_column("Max_relay_log_size", "1073741824");   // master decides
        rset->add_column("Executed_log_entries", "42");
        rset->add_column("Slave_received_heartbeats", "42");
        rset->add_column("Slave_heartbeat_period", "1");
        rset->add_column("Gtid_Slave_Pos", gtid_io_pos().to_string());
    }


    return rset->as_buffer().release();
}

void Pinloki::set_gtid_slave_pos(const maxsql::GtidList& gtid)
{
    mxb_assert(m_writer.get() == nullptr);
    if (m_inventory.rpl_state().is_included(gtid))
    {
        MXB_SERROR("The requested gtid "
                   << gtid
                   << " is already in the logs. Time travel is not supported.");
    }
    else
    {
        m_inventory.save_requested_rpl_state(gtid);
    }
}

mxq::GtidList Pinloki::gtid_io_pos() const
{
    return m_inventory.rpl_state();
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

            json_decref(js);
        }
        else
        {
            MXS_INFO("Failed to load master info JSON file: %s", err.text);
        }
    }

    return rval;
}

PurgeResult purge_binlogs(InventoryWriter* pInventory, const std::string& up_to)
{
    auto files = pInventory->file_names();
    auto up_to_ite = std::find(files.begin(), files.end(), pInventory->config().path(up_to));

    if (up_to_ite == files.end())
    {
        return PurgeResult::UpToFileNotFound;
    }
    else
    {
        auto open_inodes = get_open_inodes();
        std::sort(begin(open_inodes), end(open_inodes));

        for (auto ite = files.begin(); ite != up_to_ite; ite++)
        {
            auto inode = get_inode(*ite);

            if (std::binary_search(begin(open_inodes), end(open_inodes), inode))
            {
                MXB_SINFO("Binlog purge stopped at open file " << *ite);
                return PurgeResult::PartialPurge;
            }

            pInventory->pop_front(*ite);
            remove(ite->c_str());
        }
    }

    return PurgeResult::Ok;
}

bool Pinloki::purge_old_binlogs(mxb::Worker::Call::action_t action)
{
    if (action == mxb::Worker::Call::CANCEL)
    {
        return false;
    }

    auto now = wall_time::Clock::now();
    auto purge_before = now - config().expire_log_duration();
    const auto& file_names = m_inventory.file_names();

    auto files_to_keep = std::max(1, config().expire_log_minimum_files());      // at least one
    int max_files_to_purge = file_names.size() - files_to_keep;

    int purge_index = -1;
    for (int i = 0; i < max_files_to_purge; ++i)
    {
        auto file_time = file_mod_time(file_names[i]);
        if (file_time <= purge_before)
        {
            purge_index = i;
        }
        else
        {
            break;
        }
    }

    if (purge_index >= 0)
    {
        ++purge_index;      // purge_binlogs() purges up-to, but not including the file argument
        purge_binlogs(&m_inventory, file_names[purge_index]);
    }

    // Purge done, figure out when to do the next purge.

    auto oldest_time = oldest_logfile_time(&m_inventory);
    wall_time::TimePoint next_purge_time = oldest_time + config().expire_log_duration() + 1s;

    if (oldest_time == wall_time::TimePoint::max()
        || next_purge_time < now)
    {
        // No logs, or purge prevented due to expire_log_minimum_files.
        next_purge_time = now + m_config.purge_poll_timeout();
    }

    maxbase::Worker* worker = maxbase::Worker::get_current();
    mxb_assert(worker);

    using namespace std::chrono;
    auto wait_ms = duration_cast<milliseconds>(next_purge_time - now);

    worker->delayed_call(wait_ms, &Pinloki::purge_old_binlogs, this);

    return false;
}
}

extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    static MXS_MODULE info =
    {
        mxs::MODULE_INFO_VERSION,
        "binlogrouter",
        mxs::ModuleType::ROUTER,
        mxs::ModuleStatus::ALPHA,
        MXS_ROUTER_VERSION,
        "Pinloki",
        "V1.0.0",
        RCAP_TYPE_STMT_INPUT,
        &mxs::RouterApi<pinloki::Pinloki>::s_api,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},
        pinloki::Config::spec()
    };

    return &info;
}
