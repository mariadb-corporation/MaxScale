/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "config.hh"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <maxbase/log.hh>
#include <sstream>
#include <fstream>
#include <uuid/uuid.h>
#include <dirent.h>
#include <sys/inotify.h>
#include "pinloki.hh"
#include "find_gtid.hh"

#include "pinloki.hh"

namespace
{
namespace cfg = maxscale::config;
using namespace std::literals::chrono_literals;

cfg::Specification s_spec("pinloki", cfg::Specification::ROUTER);

cfg::ParamPath s_datadir(
    &s_spec, "datadir", "Directory where binlog files are stored",
    cfg::ParamPath::C | cfg::ParamPath::W | cfg::ParamPath::R | cfg::ParamPath::X,
    mxs::datadir() + std::string("/binlogs/"));

cfg::ParamCount s_server_id(
    &s_spec, "server_id", "Server ID sent to both slaves and the master", 1234);

cfg::ParamSeconds s_net_timeout(
    &s_spec, "net_timeout", "Network timeout", cfg::INTERPRET_AS_SECONDS, 10s);

cfg::ParamBool s_select_master(
    &s_spec, "select_master", "Automatically select the master server", false);

cfg::ParamCount s_expire_log_minimum_files(
    &s_spec, "expire_log_minimum_files", "Minimum number of files the automatic log purge keeps", 2);

cfg::ParamDuration<wall_time::Duration> s_expire_log_duration(
    &s_spec, "expire_log_duration", "Duration after which unmodified log files are purged",
    cfg::NO_INTERPRETATION, 0s);

/* Undocumented config items (for test purposes) */
cfg::ParamDuration<wall_time::Duration> s_purge_startup_delay(
    &s_spec, "purge_startup_delay", "Purge waits this long after a MaxScale startup",
    cfg::NO_INTERPRETATION, 2min);

cfg::ParamDuration<wall_time::Duration> s_purge_poll_timeout(
    &s_spec, "purge_poll_timeout", "Purge timeout/poll when expire_log_minimum_files files exist",
    cfg::NO_INTERPRETATION, 2min);
}

namespace pinloki
{
namespace
{
template<typename T>
class CallAtScopeEnd
{
public:
    CallAtScopeEnd(T at_destruct)
        : at_destruct(at_destruct)
    {
    }
    ~CallAtScopeEnd()
    {
        at_destruct();
    }
private:
    T at_destruct;
};

// Can't rely on stat() to sort files. The files can have the
// same creation/mod time
long get_file_sequence_number(const std::string& file_name)
{
    try
    {
        auto num_str = file_name.substr(file_name.find_last_of(".") + 1);
        long seq_no = 1 + std::stoi(num_str.c_str());
        return seq_no;
    }
    catch (std::exception& ex)
    {
        MXB_THROW(BinlogReadError, "Unexpected binlog file name '"
                  << file_name << "' error " << ex.what());
    }
}

std::vector<std::string> read_binlog_file_names(const std::string& binlog_dir)
{
    std::map<long, std::string> binlogs;

    DIR* pdir;
    struct dirent* pentry;

    if ((pdir = opendir(binlog_dir.c_str())) != nullptr)
    {
        auto close_dir_func = [pdir]{
            closedir(pdir);
        };
        CallAtScopeEnd<decltype(close_dir_func)> close_dir{close_dir_func};

        while ((pentry = readdir(pdir)) != nullptr)
        {
            if (pentry->d_type != DT_REG)
            {
                continue;
            }

            auto file_path = MAKE_STR(binlog_dir.c_str() << '/' << pentry->d_name);

            decltype(PINLOKI_MAGIC) magic;
            std::ifstream is{file_path.c_str(), std::ios::binary};
            if (is)
            {
                is.read(magic.data(), PINLOKI_MAGIC.size());
                if (is && magic == PINLOKI_MAGIC)
                {
                    auto seq_no = get_file_sequence_number(file_path);
                    binlogs.insert({seq_no, file_path});
                }
            }
        }
    }
    else
    {
        // This is expected if the binlog directory does not yet exist.
        MXB_SINFO("Could not open directory " << binlog_dir);
    }

    std::vector<std::string> file_names;
    file_names.reserve(binlogs.size());
    for (const auto& e : binlogs)
    {
        file_names.push_back(e.second);
    }

    return file_names;
}
}

// static
const mxs::config::Specification* Config::spec()
{
    return &s_spec;
}

std::string Config::path(const std::string& name) const
{
    if (name.find_first_of('/') == std::string::npos)
    {
        return m_binlog_dir + '/' + name;
    }

    return name;
}

std::string Config::binlog_dir_path() const
{
    return m_binlog_dir;
}

std::string Config::gtid_file_path() const
{
    return path(m_gtid_file);
}

std::string Config::requested_gtid_file_path() const
{
    return path("requested_rpl_state");
}

std::string Config::master_info_file() const
{
    return path(m_master_info_file);
}

std::string Config::inventory_file_path() const
{
    return path(m_binlog_inventory_file);
}

uint32_t Config::server_id() const
{
    return m_server_id;
}

std::chrono::seconds Config::net_timeout() const
{
    return m_net_timeout;
}

bool Config::select_master() const
{
    return m_select_master && !m_select_master_disabled;
}

void Config::disable_select_master()
{
    m_select_master_disabled = true;
}

int32_t Config::expire_log_minimum_files() const
{
    return m_expire_log_minimum_files;
}

wall_time::Duration Config::expire_log_duration() const
{
    return m_expire_log_duration;
}

wall_time::Duration Config::purge_startup_delay() const
{
    return m_purge_startup_delay;
}


wall_time::Duration Config::purge_poll_timeout() const
{
    return m_purge_poll_timeout;
}

std::string gen_uuid()
{
    char uuid_str[36 + 1];
    uuid_t uuid;

    uuid_generate_time(uuid);
    uuid_unparse_lower(uuid, uuid_str);

    return uuid_str;
}

bool Config::post_configure(const std::map<std::string, mxs::ConfigParameters>& nested_params)
{
    bool ok = false;

    // This is a workaround to the fact that the datadir is not created if the default value is used.
    mode_t mask = S_IWUSR | S_IWGRP | S_IRUSR | S_IRGRP | S_IXUSR | S_IXGRP;
    if (mxs_mkdir_all(m_binlog_dir.c_str(), mask))
    {
        m_trx_dir = m_binlog_dir + '/' + TRX_DIR;
        ok = mxs_mkdir_all(m_trx_dir.c_str(), mask);

        if (ok)
        {
            m_binlog_files.reset(new BinlogIndexUpdater(m_binlog_dir, inventory_file_path()));
            ok = m_cb();
        }
    }

    return ok;
}

Config::Config(const std::string& name, std::function<bool()> callback)
    : cfg::Configuration(name, &s_spec)
    , m_cb(callback)
{
    add_native(&Config::m_binlog_dir, &s_datadir);
    add_native(&Config::m_server_id, &s_server_id);
    add_native(&Config::m_net_timeout, &s_net_timeout);
    add_native(&Config::m_select_master, &s_select_master);
    add_native(&Config::m_expire_log_duration, &s_expire_log_duration);
    add_native(&Config::m_expire_log_minimum_files, &s_expire_log_minimum_files);
    add_native(&Config::m_purge_startup_delay, &s_purge_startup_delay);
    add_native(&Config::m_purge_poll_timeout, &s_purge_poll_timeout);
}

std::vector<std::string> Config::binlog_file_names() const
{
    return m_binlog_files->binlog_file_names();
}

void Config::set_binlogs_dirty() const
{
    m_binlog_files->set_is_dirty();
}

void Config::save_rpl_state(const maxsql::GtidList& gtids) const
{
    m_binlog_files->set_rpl_state(gtids);
}

maxsql::GtidList Config::rpl_state() const
{
    return m_binlog_files->rpl_state();
}

BinlogIndexUpdater::BinlogIndexUpdater(const std::string& binlog_dir,
                                       const std::string& inventory_file_path)
    : m_inotify_fd(inotify_init1(0))
    , m_binlog_dir(binlog_dir)
    , m_inventory_file_path(inventory_file_path)
    , m_file_names(read_binlog_file_names(m_binlog_dir))
{
    if (m_inotify_fd == -1)
    {
        MXS_SERROR("inotify_init failed: " << errno << ", " << mxb_strerror(errno));
    }
    else
    {
        m_watch = inotify_add_watch(m_inotify_fd, m_binlog_dir.c_str(), IN_CREATE | IN_DELETE);

        if (m_watch == -1)
        {
            MXS_SERROR("inotify_add_watch for directory " <<
                       m_binlog_dir.c_str() << "failed: " << errno << ", " << mxb_strerror(errno));
        }
        else
        {
            m_update_thread = std::thread(&BinlogIndexUpdater::update, this);
        }
    }
}

void BinlogIndexUpdater::set_is_dirty()
{
    m_is_dirty.store(true, std::memory_order_relaxed);
}

std::vector<std::string> BinlogIndexUpdater::binlog_file_names()
{
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    if (m_is_dirty)
    {
        m_file_names = read_binlog_file_names(m_binlog_dir);
        m_is_dirty.store(false, std::memory_order_relaxed);
    }
    return m_file_names;
}

BinlogIndexUpdater::~BinlogIndexUpdater()
{
    m_running.store(false, std::memory_order_relaxed);
    if (m_watch != -1)
    {
        inotify_rm_watch(m_inotify_fd, m_watch);
        m_update_thread.join();
    }
}

void BinlogIndexUpdater::set_rpl_state(const maxsql::GtidList& gtids)
{
    // Using the same mutex for rpl state as for file names. There
    // is very little action hitting this mutex.
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    m_rpl_state = gtids;
}

maxsql::GtidList BinlogIndexUpdater::rpl_state()
{
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    return m_rpl_state;
}

void BinlogIndexUpdater::update()
{
    const size_t SZ = 1024;
    char buffer[SZ];

    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    m_file_names = read_binlog_file_names(m_binlog_dir);
    lock.unlock();

    while (m_running.load(std::memory_order_relaxed))
    {
        auto n = ::read(m_inotify_fd, buffer, SZ);
        if (n <= 0)
        {
            continue;
        }

        lock.lock();
        auto new_names = read_binlog_file_names(m_binlog_dir);
        if (new_names != m_file_names)
        {
            m_file_names = std::move(new_names);
            std::string tmp = m_inventory_file_path + ".tmp";
            std::ofstream ofs(tmp, std::ios_base::trunc);

            for (const auto& file : m_file_names)
            {
                ofs << file << '\n';
            }

            rename(tmp.c_str(), m_inventory_file_path.c_str());
        }
        lock.unlock();
    }
}
}
