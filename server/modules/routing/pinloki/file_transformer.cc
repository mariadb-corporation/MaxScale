/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "file_transformer.hh"
#include "config.hh"
#include <maxbase/string.hh>
#include <map>
#include <sys/inotify.h>
#include <dirent.h>

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

/**
 * @brief get_file_sequence_number
 * @param file_name
 * @return sequence number or 0 for unexpected file name
 */
long get_file_sequence_number(const std::string& file_name)
{
    auto pos1 = file_name.find_last_of(".");
    auto pos2 = std::string::npos;
    if (pos1 && pos1 != std::string::npos
        && file_name.substr(pos1 + 1, std::string::npos) == COMPRESSION_EXTENSION)
    {
        pos2 = pos1;
        pos1 = file_name.find_last_of(".", pos1 - 1);
    }

    auto num_str = file_name.substr(pos1 + 1, pos2 - pos1 - 1);

    return std::atol(num_str.c_str());
}

std::vector<std::string> read_binlog_file_names(const std::string& binlog_dir)
{
    std::map<long, std::string> binlogs;

    DIR* pdir;
    struct dirent* pentry;

    if ((pdir = opendir(binlog_dir.c_str())) != nullptr)
    {
        CallAtScopeEnd close_dir{[pdir]{
            closedir(pdir);
        }};

        while ((pentry = readdir(pdir)) != nullptr)
        {
            if (pentry->d_type != DT_REG)
            {
                continue;
            }

            auto file_path = MAKE_STR(binlog_dir.c_str() << '/' << pentry->d_name);

            std::array<char, MAGIC_SIZE> magic;
            std::ifstream is{file_path.c_str(), std::ios::binary};
            if (is)
            {
                is.read(magic.data(), PINLOKI_MAGIC.size());
                if (is && (magic == PINLOKI_MAGIC || magic == ZSTD_MAGIC))
                {
                    auto seq_no = get_file_sequence_number(file_path);
                    if (seq_no)
                    {
                        binlogs.insert({seq_no, file_path});
                    }
                    else
                    {
                        MXB_SWARNING("Unexpected binlog file name '" << file_path <<
                                     "'. File ignored."
                                     " Avoid copying or otherwise changing files in the binlog"
                                     " directory. Please delete possible copies of binlogs.");
                    }
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

FileTransformer::FileTransformer(const std::string& binlog_dir,
                                         const std::string& inventory_file_path)
    : m_inotify_fd(inotify_init1(0))
    , m_binlog_dir(binlog_dir)
    , m_inventory_file_path(inventory_file_path)
    , m_file_names(read_binlog_file_names(m_binlog_dir))
{
    if (m_inotify_fd == -1)
    {
        MXB_SERROR("inotify_init failed: " << errno << ", " << mxb_strerror(errno));
    }
    else
    {
        m_watch = inotify_add_watch(m_inotify_fd, m_binlog_dir.c_str(),
                                    IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);

        if (m_watch == -1)
        {
            MXB_SERROR("inotify_add_watch for directory " <<
                       m_binlog_dir.c_str() << "failed: " << errno << ", " << mxb_strerror(errno));
        }
        else
        {
            m_update_thread = std::thread(&FileTransformer::update, this);
        }
    }
}

void FileTransformer::set_is_dirty()
{
    m_is_dirty.store(true, std::memory_order_relaxed);
}

std::vector<std::string> FileTransformer::binlog_file_names()
{
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    if (m_is_dirty)
    {
        m_file_names = read_binlog_file_names(m_binlog_dir);
        m_is_dirty.store(false, std::memory_order_relaxed);
    }
    return m_file_names;
}

FileTransformer::~FileTransformer()
{
    m_running.store(false, std::memory_order_relaxed);
    if (m_watch != -1)
    {
        inotify_rm_watch(m_inotify_fd, m_watch);
        m_update_thread.join();
    }
}

void FileTransformer::set_rpl_state(const maxsql::GtidList& gtids)
{
    // Using the same mutex for rpl state as for file names. There
    // is very little action hitting this mutex.
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    m_rpl_state = gtids;
}

maxsql::GtidList FileTransformer::rpl_state()
{
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    return m_rpl_state;
}

void FileTransformer::update()
{
    const size_t SZ = 1024;
    char buffer[SZ];

    std::unique_lock<std::mutex> lock(m_file_names_mutex, std::defer_lock);

    while (m_running.load(std::memory_order_relaxed))
    {
        auto n = ::read(m_inotify_fd, buffer, SZ);
        if (n <= 0)
        {
            continue;
        }

        lock.lock();
        auto new_names = read_binlog_file_names(m_binlog_dir);
        std::ifstream index(m_inventory_file_path);

        decltype(new_names) index_names;
        std::string line;
        while(std::getline(index, line))
        {
            index_names.push_back(line);
        }

        if (new_names != index_names)
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
