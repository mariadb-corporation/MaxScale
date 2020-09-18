/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @file qlafilter.cc - Quary Log All Filter
 *
 * QLA Filter - Query Log All. A simple query logging filter. All queries passing
 * through the filter are written to a text file.
 *
 * The filter makes no attempt to deal with query packets that do not fit
 * in a single GWBUF.
 */


#include "qlafilter.hh"

#include <cmath>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <sstream>
#include <sys/time.h>
#include <fstream>
#include <sstream>
#include <string>

#include <maxbase/alloc.h>
#include <maxbase/atomic.h>
#include <maxscale/config2.hh>
#include <maxbase/format.hh>
#include <maxscale/modinfo.hh>
#include <maxscale/modutil.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>
#include <maxscale/modulecmd.hh>
#include <maxscale/json_api.hh>

using std::string;

namespace
{

uint64_t CAPABILITIES = RCAP_TYPE_CONTIGUOUS_INPUT;

const char HEADER_ERROR[] = "Failed to print header to file %s. Error %i: '%s'.";

namespace cfg = mxs::config;

cfg::Specification s_spec(MXS_MODULE_NAME, cfg::Specification::FILTER);

cfg::ParamRegex s_match(
    &s_spec, "match", "Only log queries matching this pattern", "",
    cfg::Param::AT_STARTUP);

cfg::ParamRegex s_exclude(
    &s_spec, "exclude", "Exclude queries matching this pattern from the log", "",
    cfg::Param::AT_STARTUP);

cfg::ParamString s_user(
    &s_spec, "user", "Log queries only from this user", "",
    cfg::Param::AT_STARTUP);

cfg::ParamString s_source(
    &s_spec, "source", "Log queries only from this network address", "",
    cfg::Param::AT_STARTUP);

cfg::ParamString s_filebase(
    &s_spec, "filebase", "The basename of the output file",
    cfg::Param::AT_STARTUP);

cfg::ParamEnumMask<uint32_t> s_options(
    &s_spec, "options", "Regular expression options",
    {
        {0, "case"},
        {PCRE2_CASELESS, "ignorecase"},
        {PCRE2_EXTENDED, "extended"},
    },
    0,
    cfg::Param::AT_STARTUP);

cfg::ParamEnum<int64_t> s_log_type(
    &s_spec, "log_type", "The type of log file to use",
    {
        {QlaInstance::LOG_FILE_SESSION, "session"},
        {QlaInstance::LOG_FILE_UNIFIED, "unified"},
        {QlaInstance::LOG_FILE_STDOUT, "stdout"},
    },
    QlaInstance::LOG_FILE_SESSION,
    cfg::Param::AT_STARTUP);

cfg::ParamEnumMask<int64_t> s_log_data(
    &s_spec, "log_data", "Type of data to log in the log files",
    {
        {QlaInstance::LOG_DATA_SERVICE, "service"},
        {QlaInstance::LOG_DATA_SESSION, "session"},
        {QlaInstance::LOG_DATA_DATE, "date"},
        {QlaInstance::LOG_DATA_USER, "user"},
        {QlaInstance::LOG_DATA_QUERY, "query"},
        {QlaInstance::LOG_DATA_REPLY_TIME, "reply_time"},
        {QlaInstance::LOG_DATA_DEFAULT_DB, "default_db"},
    },
    QlaInstance::LOG_DATA_DATE | QlaInstance::LOG_DATA_USER | QlaInstance::LOG_DATA_QUERY,
    cfg::Param::AT_STARTUP);

cfg::ParamString s_newline_replacement(
    &s_spec, "newline_replacement", "Value used to replace newlines", " ",
    cfg::Param::AT_STARTUP);

cfg::ParamString s_separator(
    &s_spec, "separator", "Defines the separator between elements of a log entry", ",",
    cfg::Param::AT_STARTUP);

cfg::ParamBool s_flush(
    &s_spec, "flush", "Flush log files after every write", false,
    cfg::Param::AT_STARTUP);

cfg::ParamBool s_append(
    &s_spec, "append", "Append new entries to log files instead of overwriting them", false,
    cfg::Param::AT_STARTUP);

void print_string_replace_newlines(const char* sql_string, size_t sql_str_len,
                                   const char* rep_newline, std::stringstream* output);

bool check_replace_file(const string& filename, FILE** ppFile);
}

QlaInstance::QlaInstance(const string& name)
    : m_settings(name, this)
    , m_name(name)
    , m_rotation_count(mxs_get_log_rotation_count())
{
}

QlaInstance::Settings::Settings(const std::string& name, QlaInstance* instance)
    : mxs::config::Configuration(name, &s_spec)
    , m_instance(instance)
{
    add_native(&Settings::filebase, &s_filebase);
    add_native(&Settings::flush_writes, &s_flush);
    add_native(&Settings::append, &s_append);
    add_native(&Settings::query_newline, &s_newline_replacement);
    add_native(&Settings::separator, &s_separator);
    add_native(&Settings::user_name, &s_user);
    add_native(&Settings::source, &s_source);
    add_native(&Settings::match, &s_match);
    add_native(&Settings::exclude, &s_exclude);
    add_native(&Settings::options, &s_options);
    add_native(&Settings::log_file_data_flags, &s_log_data);
    add_native(&Settings::log_file_types, &s_log_type);
}

bool QlaInstance::Settings::post_configure()
{
    write_session_log = (log_file_types & LOG_FILE_SESSION);
    write_unified_log = (log_file_types & LOG_FILE_UNIFIED);
    write_stdout_log = (log_file_types & LOG_FILE_STDOUT);
    session_data_flags = log_file_data_flags & ~LOG_DATA_SESSION;
    exclude.set_options(options);
    match.set_options(options);
    return m_instance->post_configure();
}

QlaInstance::~QlaInstance()
{
    if (m_unified_fp != NULL)
    {
        fclose(m_unified_fp);
    }
}

QlaFilterSession::QlaFilterSession(QlaInstance& instance, MXS_SESSION* session, SERVICE* service)
    : mxs::FilterSession(session, service)
    , m_instance(instance)
    , m_user(session_get_user(session))
    , m_remote(session_get_remote(session))
    , m_service(session->service->name())
    , m_ses_id(session->id())
    , m_rotation_count(mxs_get_log_rotation_count())
{
}

QlaFilterSession::~QlaFilterSession()
{
    if (m_logfile)
    {
        fclose(m_logfile);
        m_logfile = nullptr;
    }
    m_event_data.clear();

    // File should be closed and event data freed by now
    mxb_assert(m_logfile == NULL && m_event_data.has_message == false);
}

bool QlaInstance::post_configure()
{
    // Try to open the unified log file
    if (m_settings.write_unified_log)
    {
        m_unified_filename = m_settings.filebase + ".unified";
        // Open the file. It is only closed at program exit.
        if (!open_unified_logfile())
        {
            return false;
        }
    }

    if (m_settings.write_stdout_log)
    {
        write_stdout_log_entry(generate_log_header(m_settings.log_file_data_flags));
    }

    return true;
}

QlaInstance* QlaInstance::create(const char* name, mxs::ConfigParameters* params)
{
    return new QlaInstance(name);
}

mxs::FilterSession* QlaInstance::newSession(MXS_SESSION* session, SERVICE* service)
{
    auto my_session = new(std::nothrow) QlaFilterSession(*this, session, service);
    if (my_session && !my_session->prepare())
    {
        delete my_session;
        my_session = nullptr;
    }
    return my_session;
}

bool QlaFilterSession::prepare()
{
    const auto& settings = m_instance.m_settings;
    bool hostname_ok = settings.source.empty() || (m_remote == settings.source);
    bool username_ok = settings.user_name.empty() || (m_user == settings.user_name);
    m_active = hostname_ok && username_ok;

    bool error = false;

    if (m_active & settings.write_session_log)
    {
        // Only open the session file if the corresponding mode setting is used.
        m_filename = mxb::string_printf("%s.%" PRIu64, settings.filebase.c_str(), m_ses_id);
        m_logfile = m_instance.open_session_log_file(m_filename);
        if (!m_logfile)
        {
            error = true;
        }
    }
    return !error;
}

bool QlaInstance::read_to_json(int start, int end, json_t** output) const
{
    bool rval = false;
    if (m_settings.write_unified_log)
    {
        mxb_assert(m_unified_fp && !m_unified_filename.empty());
        std::ifstream file(m_unified_filename);

        if (file)
        {
            json_t* arr = json_array();
            // TODO: Add integer type to modulecmd
            int current = 0;

            /** Skip lines we don't want */
            for (std::string line; current < start && std::getline(file, line); current++)
            {
            }

            /** Read lines until either EOF or line count is reached */
            for (std::string line; std::getline(file, line) && (current < end || end == 0); current++)
            {
                json_array_append_new(arr, json_string(line.c_str()));
            }

            *output = arr;
            rval = true;
        }
        else
        {
            *output = mxs_json_error("Failed to open file '%s'", m_unified_filename.c_str());
        }
    }
    else
    {
        *output = mxs_json_error("Filter '%s' does not have unified log file enabled", m_name.c_str());
    }
    return rval;
}

json_t* QlaInstance::diagnostics() const
{
    return json_null();
}

uint64_t QlaInstance::getCapabilities() const
{
    return CAPABILITIES;
}

json_t* QlaFilterSession::diagnostics() const
{
    json_t* rval = json_object();
    json_object_set_new(rval, "session_filename", json_string(m_filename.c_str()));
    return rval;
}

void QlaInstance::check_reopen_file(const string& filename, uint64_t data_flags, FILE** ppFile) const
{
    if (check_replace_file(filename, ppFile))
    {
        auto fp = *ppFile;
        // New file created, print the log header.
        string header = generate_log_header(data_flags);
        if (!write_to_logfile(fp, header))
        {
            MXS_ERROR(HEADER_ERROR, filename.c_str(), errno, mxs_strerror(errno));
            fclose(fp);
            fp = nullptr;
            *ppFile = fp;
        }
    }
    // Either the old file existed or file creation failed.
}

void QlaInstance::check_reopen_session_file(const std::string& filename, FILE** ppFile) const
{
    check_reopen_file(filename, m_settings.session_data_flags, ppFile);
}

bool QlaInstance::match_exclude(const char* sql, int len)
{
    return (!m_settings.match || m_settings.match.match(sql, (size_t)len))
           && (!m_settings.exclude || !m_settings.exclude.match(sql, (size_t)len));
}

/**
 * Write QLA log entry/entries to disk
 *
 * @params elems Log entry contents
 */
void QlaFilterSession::write_log_entries(const LogEventElems& elems)
{
    if (m_instance.m_settings.write_session_log)
    {
        int global_rot_count = mxs_get_log_rotation_count();
        if (global_rot_count > m_rotation_count)
        {
            m_rotation_count = global_rot_count;
            m_instance.check_reopen_session_file(m_filename, &m_logfile);
        }

        if (m_logfile)
        {
            string entry = generate_log_entry(m_instance.m_settings.session_data_flags, elems);
            write_session_log_entry(entry);
        }
    }

    if (m_instance.m_settings.write_unified_log || m_instance.m_settings.write_stdout_log)
    {
        string unified_log_entry =
            generate_log_entry(m_instance.m_settings.log_file_data_flags, elems);

        if (m_instance.m_settings.write_unified_log)
        {
            m_instance.write_unified_log_entry(unified_log_entry);
        }

        if (m_instance.m_settings.write_stdout_log)
        {
            m_instance.write_stdout_log_entry(unified_log_entry);
        }
    }
}

int QlaFilterSession::routeQuery(GWBUF* queue)
{
    char* query = NULL;
    int query_len = 0;

    if (m_active && modutil_extract_SQL(queue, &query, &query_len)
        && m_instance.match_exclude(query, query_len))
    {
        const uint32_t data_flags = m_instance.m_settings.log_file_data_flags;
        LogEventData& event = m_event_data;
        if (data_flags & QlaInstance::LOG_DATA_DATE)
        {
            // Print current date to a buffer. Use the buffer in the event data struct even if execution time
            // is not needed.
            const time_t utc_seconds = time(NULL);
            tm local_time;
            localtime_r(&utc_seconds, &local_time);
            strftime(event.query_date, LogEventData::DATE_BUF_SIZE, "%F %T", &local_time);
        }

        if (data_flags & QlaInstance::LOG_DATA_REPLY_TIME)
        {
            // Have to measure reply time from server. Save query data for printing during clientReply.
            // If old event data exists, it is erased. This only happens if client sends a query before
            // receiving reply to previous query.
            if (event.has_message)
            {
                event.clear();
            }
            clock_gettime(CLOCK_MONOTONIC, &event.begin_time);
            if (data_flags & QlaInstance::LOG_DATA_QUERY)
            {
                event.query_clone = gwbuf_clone(queue);
            }
            event.has_message = true;
        }
        else
        {
            // If execution times are not logged, write the log entry now.
            LogEventElems elems(event.query_date, query, query_len);
            write_log_entries(elems);
        }
    }
    /* Pass the query downstream */
    return mxs::FilterSession::routeQuery(queue);
}

int QlaFilterSession::clientReply(GWBUF* queue, const mxs::ReplyRoute& down, const mxs::Reply& reply)
{
    LogEventData& event = m_event_data;
    if (event.has_message)
    {
        const uint32_t data_flags = m_instance.m_settings.log_file_data_flags;
        mxb_assert(data_flags & QlaInstance::LOG_DATA_REPLY_TIME);

        char* sql = nullptr;
        int sql_len = 0;
        if (data_flags & QlaInstance::LOG_DATA_QUERY)
        {
            modutil_extract_SQL(event.query_clone, &sql, &sql_len);
        }

        // Calculate elapsed time in milliseconds.
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);   // Gives time in seconds + nanoseconds
        double elapsed_ms = 1E3 * (now.tv_sec - event.begin_time.tv_sec)
            + (now.tv_nsec - event.begin_time.tv_nsec) / (double)1E6;

        LogEventElems elems(event.query_date, sql, sql_len, std::floor(elapsed_ms + 0.5));
        write_log_entries(elems);
        event.clear();
    }
    return mxs::FilterSession::clientReply(queue, down, reply);
}

FILE* QlaInstance::open_session_log_file(const string& filename) const
{
    return open_log_file(m_settings.session_data_flags, filename);
}

bool QlaInstance::open_unified_logfile()
{
    mxb_assert(!m_unified_fp);
    m_unified_fp = open_log_file(m_settings.log_file_data_flags, m_unified_filename);
    return m_unified_fp != nullptr;
}

/**
 * Open a log file for writing and print a header if file did not exist.
 *
 * @param   data_flags  Data save settings flags
 * @param   filename    Target file path
 * @return  A valid file on success, null otherwise.
 */
FILE* QlaInstance::open_log_file(uint64_t data_flags, const string& filename) const
{
    auto zfilename = filename.c_str();
    bool file_existed = false;
    FILE* fp = NULL;
    if (m_settings.append == false)
    {
        // Just open the file (possibly overwriting) and then print header.
        fp = fopen(zfilename, "w");
    }
    else
    {
        /**
         *  Using fopen() with 'a+' means we will always write to the end but can read
         *  anywhere.
         */
        if ((fp = fopen(zfilename, "a+")) != NULL)
        {
            // Check to see if file already has contents
            fseek(fp, 0, SEEK_END);
            if (ftell(fp) > 0)
            {
                file_existed = true;
            }
        }
    }

    if (!fp)
    {
        MXS_ERROR("Failed to open file '%s'. Error %i: '%s'.", zfilename, errno, mxs_strerror(errno));
    }
    else if (!file_existed && data_flags != 0)
    {
        string header = generate_log_header(data_flags);
        if (!write_to_logfile(fp, header))
        {
            MXS_ERROR(HEADER_ERROR, zfilename, errno, mxs_strerror(errno));
            fclose(fp);
            fp = nullptr;
        }
    }
    return fp;
}

string QlaInstance::generate_log_header(uint64_t data_flags) const
{
    // Print a header.
    const char SERVICE[] = "Service";
    const char SESSION[] = "Session";
    const char DATE[] = "Date";
    const char USERHOST[] = "User@Host";
    const char QUERY[] = "Query";
    const char REPLY_TIME[] = "Reply_time";
    const char DEFAULT_DB[] = "Default_db";

    std::stringstream header;
    string curr_sep;    // Use empty string as the first separator
    const string& real_sep = m_settings.separator;

    if (data_flags & LOG_DATA_SERVICE)
    {
        header << SERVICE;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_SESSION)
    {
        header << curr_sep << SESSION;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_DATE)
    {
        header << curr_sep << DATE;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_USER)
    {
        header << curr_sep << USERHOST;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_REPLY_TIME)
    {
        header << curr_sep << REPLY_TIME;
        curr_sep = real_sep;
    }
    if (data_flags & LOG_DATA_QUERY)
    {
        header << curr_sep << QUERY;
    }
    if (data_flags & LOG_DATA_DEFAULT_DB)
    {
        header << curr_sep << DEFAULT_DB;
    }
    header << '\n';
    return header.str();
}

string QlaFilterSession::generate_log_entry(uint64_t data_flags, const LogEventElems& elems) const
{
    /* Printing to the file in parts would likely cause garbled printing if several threads write
     * simultaneously, so we have to first print to a string. */
    std::stringstream output;
    string curr_sep;    // Use empty string as the first separator
    const string& real_sep = m_instance.m_settings.separator;

    if (data_flags & QlaInstance::LOG_DATA_SERVICE)
    {
        output << m_service;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_SESSION)
    {
        output << curr_sep << m_ses_id;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_DATE)
    {
        output << curr_sep << elems.date_string;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_USER)
    {
        output << curr_sep << m_user << "@" << m_remote;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_REPLY_TIME)
    {
        output << curr_sep << elems.elapsed_ms;
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_QUERY)
    {
        output << curr_sep;
        if (!m_instance.m_settings.query_newline.empty())
        {
            print_string_replace_newlines(elems.query, elems.querylen,
                                          m_instance.m_settings.query_newline.c_str(),
                                          &output);
        }
        else
        {
            // The newline replacement is an empty string so print the query as is
            output.write(elems.query, elems.querylen);
        }
        curr_sep = real_sep;
    }
    if (data_flags & QlaInstance::LOG_DATA_DEFAULT_DB)
    {
        std::string db = m_pSession->database().empty() ? "(none)" : m_pSession->database();

        output << curr_sep << db;
        curr_sep = real_sep;
    }
    output << "\n";
    return output.str();
}

bool QlaInstance::write_to_logfile(FILE* fp, const std::string& contents) const
{
    bool error = false;
    int written = fprintf(fp, "%s", contents.c_str());
    if (written < 0)
    {
        error = true;
    }
    else if (m_settings.flush_writes && (fflush(fp) != 0))
    {
        error = true;
    }

    return !error;
}

/**
 * Write an entry to the session log file.
 *
 * @param   entry  Log entry contents
 */
void QlaFilterSession::write_session_log_entry(const string& entry)
{
    mxb_assert(m_logfile != NULL);
    if (!m_instance.write_to_logfile(m_logfile, entry))
    {
        if (!m_write_error_logged)
        {
            MXS_ERROR("Failed to write to session log file '%s'. Suppressing further similar warnings.",
                      m_filename.c_str());
            m_write_error_logged = true;
        }
    }
}

/**
 * Write an entry to the shared log file.
 *
 * @param   entry  Log entry contents
 */
void QlaInstance::write_unified_log_entry(const string& entry)
{
    std::lock_guard<std::mutex> guard(m_file_lock);
    int global_rot_count = mxs_get_log_rotation_count();
    if (global_rot_count > m_rotation_count)
    {
        m_rotation_count = global_rot_count;
        check_reopen_file(m_unified_filename, m_settings.log_file_data_flags, &m_unified_fp);
    }

    if (m_unified_fp)
    {
        if (!write_to_logfile(m_unified_fp, entry))
        {
            if (!m_write_error_logged)
            {
                MXS_ERROR("Failed to write to unified log file '%s'. Suppressing further similar warnings.",
                          m_unified_filename.c_str());
                m_write_error_logged = true;
            }
        }
    }
}

/**
 * Write an entry to stdout.
 *
 * @param entry Log entry contents
 */
void QlaInstance::write_stdout_log_entry(const string& entry) const
{
    std::cout << entry;

    if (m_settings.flush_writes)
    {
        std::cout.flush();
    }
}

namespace
{

void print_string_replace_newlines(const char* sql_string,
                                   size_t sql_str_len,
                                   const char* rep_newline,
                                   std::stringstream* output)
{
    mxb_assert(output);
    size_t line_begin = 0;
    size_t search_pos = 0;
    while (search_pos < sql_str_len)
    {
        int line_end_chars = 0;
        // A newline is either \r\n, \n or \r
        if (sql_string[search_pos] == '\r')
        {
            if (search_pos + 1 < sql_str_len && sql_string[search_pos + 1] == '\n')
            {
                // Got \r\n
                line_end_chars = 2;
            }
            else
            {
                // Just \r
                line_end_chars = 1;
            }
        }
        else if (sql_string[search_pos] == '\n')
        {
            // Just \n
            line_end_chars = 1;
        }

        if (line_end_chars > 0)
        {
            // Found line ending characters, write out the line excluding line end.
            output->write(&sql_string[line_begin], search_pos - line_begin);
            *output << rep_newline;
            // Next line begins after line end chars
            line_begin = search_pos + line_end_chars;
            // For \r\n, advance search_pos
            search_pos += line_end_chars - 1;
        }

        search_pos++;
    }

    // Print anything left
    if (line_begin < sql_str_len)
    {
        output->write(&sql_string[line_begin], sql_str_len - line_begin);
    }
}

/**
 * Open a file if it doesn't exist.
 *
 * @param filename Filename
 * @param ppFile Double pointer to old file. The file can be null.
 * @return True if new file was opened successfully. False, if file already existed or if new file
 * could not be opened. If false is returned, the caller should check that the file object exists.
 */
bool check_replace_file(const string& filename, FILE** ppFile)
{
    auto zfilename = filename.c_str();
    const char retry_later[] = "Logging to file is disabled. The operation will be retried later.";

    bool newfile = false;
    // Check if file exists and create it if not.
    int fd = open(zfilename, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (fd < 0)
    {
        // EEXIST is the expected error code.
        if (errno != EEXIST)
        {
            MXS_ERROR("Could not open log file '%s'. open() failed with error code %i: '%s'. %s",
                      zfilename, errno, mxs_strerror(errno), retry_later);
            // Do not close the existing file in this case since it was not touched. Likely though,
            // writing to it will fail.
        }
        // Otherwise the file already exists and the existing file stream should be valid.
    }
    else
    {
        MXS_INFO("Log file '%s' recreated.", zfilename);
        // File was created. Close the original file stream since it's pointing to a moved file.
        auto fp = *ppFile;
        if (fp)
        {
            fclose(fp);
        }
        fp = fdopen(fd, "w");
        if (fp)
        {
            newfile = true;
        }
        else
        {
            MXS_ERROR("Could not convert file descriptor of '%s' to stream. fdopen() "
                      "failed with error code %i: '%s'. %s",
                      filename.c_str(), errno, mxs_strerror(errno), retry_later);
            ::close(fd);
        }
        *ppFile = fp;
    }
    return newfile;
}

bool cb_log(const MODULECMD_ARG* argv, json_t** output)
{
    mxb_assert(argv->argc > 0);
    mxb_assert(argv->argv[0].type.type == MODULECMD_ARG_FILTER);

    MXS_FILTER_DEF* filter = argv[0].argv->value.filter;
    QlaInstance* instance = reinterpret_cast<QlaInstance*>(filter_def_get_instance(filter));
    int start = argv->argc > 1 ? atoi(argv->argv[1].value.string) : 0;
    int end = argv->argc > 2 ? atoi(argv->argv[2].value.string) : 0;

    return instance->read_to_json(start, end, output);
}
}

/**
 * The module entry point routine.
 *
 * @return The module object
 */
extern "C" MXS_MODULE* MXS_CREATE_MODULE()
{
    modulecmd_arg_type_t args[] =
    {
        {
            MODULECMD_ARG_FILTER | MODULECMD_ARG_NAME_MATCHES_DOMAIN,
            "Filter to read logs from"
        },
        {
            MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,
            "Start reading from this line"
        },
        {
            MODULECMD_ARG_STRING | MODULECMD_ARG_OPTIONAL,
            "Stop reading at this line (exclusive)"
        }
    };

    modulecmd_register_command(MXS_MODULE_NAME, "log", MODULECMD_TYPE_PASSIVE,
                               cb_log, 3, args,
                               "Show unified log file as a JSON array");

    static const char description[] = "A simple query logging filter";
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        description,
        "V1.1.1",
        CAPABILITIES,
        &QlaInstance::s_object,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        {{nullptr}},    /* Legacy parameters */
        &s_spec
    };

    return &info;
}
