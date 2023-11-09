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
#include "inventory.hh"
#include <maxbase/string.hh>
#include <map>
#include <sys/inotify.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <poll.h>

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
                    // TODO both compressed and non-compressed should not exist
                    //      at the same time, unless someone compresses by hand (allowed).
                    //      Give priority to the uncompressed one? In fact, the list of files
                    //      should always be without compression extensions, but that requires
                    //      a bunch of changes, mostly in this file only though (and remove
                    //      all calls to strip_extension() elsewhere).
                    //      Hm, compression will move the compressed file into place, and then
                    //      delete the uncompressed one, so both could be visible here.
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

struct FileParts
{
    std::string path;
    std::string file;
};

FileParts split_file_path(const std::string& file_path)
{
    auto i = file_path.find_last_of('/');
    if (i == std::string::npos)
    {
        return FileParts();
    }

    return FileParts{file_path.substr(0, i), file_path.substr(i + 1)};
}
}

FileTransformer::FileTransformer(const Config& config)
    : m_inotify_fd(inotify_init1(0))
    , m_config(config)
{
    update_file_list();

    m_next_purge_time = wall_time::Clock::now() + m_config.purge_startup_delay();

    if (m_inotify_fd == -1)
    {
        MXB_SERROR("inotify_init failed: " << errno << ", " << mxb_strerror(errno));
    }
    else
    {
        m_watch = inotify_add_watch(m_inotify_fd, m_config.binlog_dir().c_str(),
                                    IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO);

        if (m_watch == -1)
        {
            MXB_SERROR("inotify_add_watch for directory " <<
                       m_config.binlog_dir().c_str() << "failed: " << errno << ", " << mxb_strerror(errno));
        }
        else
        {
            m_update_thread = std::thread(&FileTransformer::run, this);
        }
    }
}

std::vector<std::string> FileTransformer::binlog_file_names()
{
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
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

void FileTransformer::run()
{
    using namespace std::chrono;

    const size_t SZ = 4096;
    char buffer[SZ];
    // Setting pollfd::revents to POLLIN, a return bit from poll(). In case
    // purge is not enabled, it ensures a (blocking) read from the inotify
    // fd is always made.
    pollfd pfd{m_inotify_fd, POLLIN, POLLIN};

    while (m_running.load(std::memory_order_relaxed))
    {
        if (m_config.expire_log_duration().count())
        {
            auto now = wall_time::Clock::now();
            int millisecs = duration_cast<milliseconds>(m_next_purge_time - now).count();
            if (millisecs > 0)
            {
                poll(&pfd, 1, millisecs);
            }

            if (m_next_purge_time <= wall_time::Clock::now())
            {
                purge_expired_binlogs();
            }
        }

        if (pfd.revents & POLLIN)
        {
            // Empty the notification data. We do not really care what
            // events there are, the existence of data is just a trigger.
            ::read(m_inotify_fd, buffer, SZ);
        }

        update_file_list();

        update_compression();
    }
}

wall_time::TimePoint FileTransformer::oldest_logfile_time()
{
    auto ret = wall_time::TimePoint::min();
    if (!m_file_names.empty())
    {
        ret = file_mod_time(first_string(m_file_names));
    }

    return ret;
}

void FileTransformer::purge_expired_binlogs()
{
    auto now = wall_time::Clock::now();
    auto purge_before = now - m_config.expire_log_duration();

    auto files_to_keep = std::max(1, m_config.expire_log_minimum_files());      // at least one
    int max_files_to_purge = m_file_names.size() - files_to_keep;

    int purge_index = -1;
    for (int i = 0; i < max_files_to_purge; ++i)
    {
        auto file_time = file_mod_time(m_file_names[i]);
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
        purge_binlogs(m_config, m_file_names[purge_index]);
    }

    // Purge done, figure out when to do the next purge.

    auto oldest_time = oldest_logfile_time();
    m_next_purge_time = oldest_time + m_config.expire_log_duration() + 1s;

    if (oldest_time == wall_time::TimePoint::min()
        || m_next_purge_time < now)
    {
        // No logs, or purge prevented due to expire_log_minimum_files.
        m_next_purge_time = now + m_config.purge_poll_timeout();
    }
}

void FileTransformer::update_file_list()
{
    auto new_names = read_binlog_file_names(m_config.binlog_dir());
    std::ifstream index(m_config.inventory_file_path());

    std::vector<std::string> index_names;
    std::string line;
    while (std::getline(index, line))
    {
        index_names.push_back(line);
    }

    if (new_names != index_names)
    {
        std::string tmp = m_config.inventory_file_path() + ".tmp";
        std::ofstream ofs(tmp, std::ios_base::trunc);

        for (const auto& file : m_file_names)
        {
            ofs << file << '\n';
        }

        rename(tmp.c_str(), m_config.inventory_file_path().c_str());
    }

    // Move the new list unconditionally, it ensures the list is populated at
    // startup and that the file and in-memory contents truly are the same.
    std::unique_lock<std::mutex> lock(m_file_names_mutex);
    m_file_names = std::move(new_names);
}

static std::string compr_err_str(const std::string file_name, const maxbase::Compressor& c)
{
    return "Compression failed for " + file_name + ' ' + maxbase::to_string(c.status())
           + (c.last_comp_error() ? " : "s + c.last_comp_error_str() : ""s);
}

static std::string make_temp_compression_name(const std::string& file_path)
{
    auto parts = split_file_path(file_path);
    return parts.path + '/' + COMPRESSION_DIR + '/'
           + parts.file + '.' + COMPRESSION_ONGOING_EXTENSION;
}

void FileTransformer::update_compression()
{
    if (m_config.compression_algorithm() == mxb::CompressionAlgorithm::ZSTANDARD
        && (!m_compression_future.valid()
            || m_compression_future.wait_for(0s) == std::future_status::ready))
    {
        ssize_t ncheck = m_file_names.size() - m_config.noncompressed_number_of_files();
        for (ssize_t i = 0; i < ncheck; ++i)
        {
            if (!has_extension(m_file_names[i], COMPRESSION_EXTENSION))
            {
                m_compression_future = std::async(&FileTransformer::compress_file, this, m_file_names[i]);
                break;
            }
        }
    }
}

maxbase::CompressionStatus FileTransformer::compress_file(const std::string& file_path)
{
    std::ifstream in(file_path);
    std::string temp_compress_name = make_temp_compression_name(file_path);
    std::string compressed_name = file_path + '.' + COMPRESSION_EXTENSION;
    std::ofstream out(temp_compress_name);
    maxbase::Compressor compressor(3);      // TODO, add level to config, maybe.

    if (compressor.status() == maxbase::CompressionStatus::OK)
    {
        compressor.compress(in, out);
        if (compressor.status() != maxbase::CompressionStatus::OK)
        {
            MXB_SWARNING(compr_err_str(file_path, compressor));
            remove(temp_compress_name.c_str());
        }
        else if (rename(temp_compress_name.c_str(), compressed_name.c_str()))
        {
            MXB_SWARNING("Failed to move " << temp_compress_name <<
                         " to " << compressed_name << " : " << mxb_strerror(errno));
            remove(temp_compress_name.c_str());
        }
        else if (remove(file_path.c_str()))
        {
            MXB_SWARNING("Failed to delete " << file_path <<
                         " that has been compressed to " << compressed_name);
        }
    }
    else
    {
        remove(temp_compress_name.c_str());
        MXB_SERROR(compr_err_str(file_path, compressor));
    }

    return compressor.status();
}

PurgeResult purge_binlogs(const Config& config, const std::string& up_to)
{
    auto files = config.binlog_file_names();
    auto up_to_ite = std::find(files.begin(), files.end(), config.path(up_to));

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

            remove(ite->c_str());
        }
    }

    return PurgeResult::Ok;
}
}
