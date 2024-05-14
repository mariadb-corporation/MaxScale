/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
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
#include <filesystem>
#include <sys/inotify.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <poll.h>

namespace fs = std::filesystem;

namespace pinloki
{

namespace
{

/** Last modification time of file_name, or wall_time::TimePoint::min() on error
 *  and errno is set.
 */
wall_time::TimePoint file_mod_time(const std::string& file_name)
{
    errno = 0;
    auto ret = wall_time::TimePoint::min();
    struct stat file_stat;

    if (stat(file_name.c_str(), &file_stat) == 0)
    {
        ret = mxb::timespec_to_time_point<wall_time::Clock>(file_stat.st_mtim);
    }

    return ret;
}

/** Returns -1 on error and errno is set.
 */
ssize_t file_size(const std::string& file_name)
{
    errno = 0;
    ssize_t size = -1;
    struct stat file_stat;
    if (stat(file_name.c_str(), &file_stat) == 0)
    {
        size = file_stat.st_size;
    }

    return size;
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

/** Make a list of binlog files. Prefer compressed versions if both happen to exist
 *  at the same time. If something reads the file with BinlogFile, it will open the
 *  non-compressed file if it still exists.
 *  The files are sorted by their sequence number.
 *
 *  TODO: one could add more checks, for example for the case where someone deletes
 *        files by hand (it happens) in the middle of the sequence. A warning could
 *        be issued here, and the purge could delete files before the sequence "breach".
 */
std::vector<std::string> read_binlog_file_names(const std::string& binlog_dir)
{
    std::vector<std::string> dir_entries;
    std::map<long, std::string> binlogs;

    if (auto pdir = opendir(binlog_dir.c_str()); pdir != nullptr)
    {
        while (auto pentry = readdir(pdir))
        {
            if (pentry->d_type == DT_REG)
            {
                dir_entries.push_back(pentry->d_name);
            }
        }

        closedir(pdir);
    }
    else
    {
        // This is expected if the binlog directory does not yet exist.
        MXB_SINFO("Could not open directory " << binlog_dir);
        return dir_entries;     // an empty vector
    }

    for (const auto& entry : dir_entries)
    {
        auto seq_no = get_file_sequence_number(entry);
        if (seq_no)
        {
            auto file_path = MAKE_STR(binlog_dir.c_str() << '/' << entry);
            std::ifstream is{file_path.c_str()};
            if (is)
            {
                std::array<char, MAGIC_SIZE> magic;
                is.read(magic.data(), MAGIC_SIZE);
                if (is.gcount() == MAGIC_SIZE && (magic == PINLOKI_MAGIC || magic == ZSTD_MAGIC))
                {

                    auto p = binlogs.insert({seq_no, file_path});
                    // Prefer the compressed version in the list
                    if (p.second == false && magic == ZSTD_MAGIC)
                    {
                        p.first->second = file_path;
                    }
                }
            }
        }
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

//  Move a file 'from' to file 'to'. 'from' and to 'to' can refer to
//  different file systems.
// - safe_file_move() will copy the file first, verify the copy and
//   finally delete 'from'. In a crash, both files may exist.
// - It is expected that the client will retry safe_file_move() after
//   a program restart if 'from' still exists.
// - If 'to' already exists it is overwritten.
bool safe_file_move(const std::string& from, const std::string& to)
{
    bool move_ok = false;
    auto from_sz = file_size(from);

    if (from_sz == -1)
    {
        MXB_SERROR("Could not open '" << from << "' for moving: " << mxb_strerror(errno));
        return false;
    }

    try
    {
        fs::copy_file(from, to, fs::copy_options::overwrite_existing);      // may throw

        auto to_sz = file_size(to);

        if (to_sz == -1)
        {
            MXB_SERROR("Copy '" << from << "' to '" << to << "' failed: " << mxb_strerror(errno));
        }
        else if (from_sz != to_sz)
        {
            MXB_SERROR("Incomplete copy from '" << from << "' to '" << to << "' aborting move operation");
            remove(to.c_str());
        }
        else
        {
            if (remove(from.c_str()) != 0)
            {
                MXB_SWARNING("Remove of '" << from << "' failed during move to '" << to
                                           << "' Error: " << mxb_strerror(errno)
                                           << "' The copy '" << to
                                           << "'is good. If this message repeats check the two files"
                                           << " and remove '" << from
                                           << "' if it is certain the copy is good.");
            }
            move_ok = true;
        }
    }
    catch (fs::filesystem_error& ex)
    {
        MXB_SERROR("Filesystem error in safe_file_move() from '"
                   << from << "' to '" << to << "' Error: " << ex.what());
        remove(to.c_str());
    }

    return move_ok;
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
        if (m_config.expire_log_duration().count()
            || m_config.compression_algorithm() != mxb::CompressionAlgorithm::NONE)
        {
            auto now = wall_time::Clock::now();
            int millisecs = 2000;
            if (poll(&pfd, 1, millisecs) == -1)
            {
                MXB_SERROR("Binlogrouter: poll of inotify fd failed."
                           " This is likely a FATAL error if it repeats,"
                           " in which case maxscale should be restarted. Error: " << mxb_strerror(errno));
                mxb_assert(!true);
                std::this_thread::sleep_for(1s);    // don't use 100% CPU nor flood the log
                continue;
            }

            purge_expired_binlogs();
        }

        if (pfd.revents & POLLIN)
        {
            // Empty the notification data. We do not really care what
            // events there are, the existence of data is just a trigger.
            if (::read(m_inotify_fd, buffer, SZ) == -1)
            {
                MXB_SERROR("Binlogrouter: read of inotify fd failed."
                           " This is likely a FATAL error if it repeats,"
                           " in which case maxscale should be restarted. Error: " << mxb_strerror(errno));
                mxb_assert(!true);
                std::this_thread::sleep_for(1s);    // don't use 100% CPU nor flood the log
                continue;
            }
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
    if (!m_config.expire_log_duration().count())
    {
        return;
    }

    auto now = wall_time::Clock::now();

    if (m_next_purge_time > now)
    {
        return;
    }

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

        for (const auto& file : new_names)
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
        ssize_t ncheck = m_file_names.size() - m_config.number_of_noncompressed_files();
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
        else if (rename(temp_compress_name.c_str(), compressed_name.c_str()) != 0)
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

            if (config.expiration_mode() == ExpirationMode::ARCHIVE)
            {
                auto parts = split_file_path(*ite);
                auto archived_name = config.archivedir() + '/' + parts.file;
                if (!safe_file_move(ite->c_str(), archived_name.c_str()))
                {
                    MXB_SERROR(
                        "Could not archive (move) '" << *ite << "' to '" << archived_name <<
                            "' Please check that your file system is good, and specifically" <<
                            " that the archive directory '" << config.archivedir() << "' is correctly" <<
                            " configured (correct path) and that the directory is mounted.");
                }
            }
            else
            {
                if (remove(ite->c_str()) != 0)
                {
                    MXB_SWARNING("Failed to remove expired binlog file '"
                                 << *ite << "' Error: " << mxb_strerror(errno));
                }
            }
        }
    }

    return PurgeResult::Ok;
}
}
