/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
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

#include <maxscale/alloc.h>
#include <maxbase/atomic.h>
#include <maxbase/format.hh>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/service.hh>
#include <maxscale/utils.h>
#include <maxscale/modulecmd.hh>
#include <maxscale/json_api.h>

using std::string;

/* Default values for logged data */
#define LOG_DATA_DEFAULT "date,user,query"

namespace
{

const char HEADER_ERROR[] = "Failed to print header to file %s. Error %i: '%s'.";

const char PARAM_MATCH[] = "match";
const char PARAM_EXCLUDE[] = "exclude";
const char PARAM_USER[] = "user";
const char PARAM_SOURCE[] = "source";
const char PARAM_FILEBASE[] = "filebase";
const char PARAM_OPTIONS[] = "options";
const char PARAM_LOG_TYPE[] = "log_type";
const char PARAM_LOG_DATA[] = "log_data";
const char PARAM_FLUSH[] = "flush";
const char PARAM_APPEND[] = "append";
const char PARAM_NEWLINE[] = "newline_replacement";
const char PARAM_SEPARATOR[] = "separator";

const MXS_ENUM_VALUE option_values[] =
{
    {"ignorecase", PCRE2_CASELESS},
    {"case",       0             },
    {"extended",   PCRE2_EXTENDED},
    {NULL}
};

const MXS_ENUM_VALUE log_type_values[] =
{
    {"session", QlaInstance::LOG_FILE_SESSION},
    {"unified", QlaInstance::LOG_FILE_UNIFIED},
    {NULL}
};

const MXS_ENUM_VALUE log_data_values[] =
{
    {"service",    QlaInstance::LOG_DATA_SERVICE   },
    {"session",    QlaInstance::LOG_DATA_SESSION   },
    {"date",       QlaInstance::LOG_DATA_DATE      },
    {"user",       QlaInstance::LOG_DATA_USER      },
    {"query",      QlaInstance::LOG_DATA_QUERY     },
    {"reply_time", QlaInstance::LOG_DATA_REPLY_TIME},
    {NULL}
};

void print_string_replace_newlines(const char* sql_string, size_t sql_str_len,
                                   const char* rep_newline, std::stringstream* output);

/**
 * Open a file if it doesn't exist.
 *
 * @param filename Filename
 * @param ppFile Double pointer to old file. The file can be null.
 * @return True if new file was opened successfully. False, if file already existed or if new file
 * could not be opened. If false is returned, the caller should check that the file object exists.
 */
bool check_replace_file(const string& filename, FILE** ppFile);

}

QlaInstance::QlaInstance(const string& name, MXS_CONFIG_PARAMETER* params)
    : m_settings(params)
    , m_name(name)
    , m_session_data_flags(m_settings.log_file_data_flags & ~LOG_DATA_SESSION)
{
}

QlaInstance::Settings::Settings(MXS_CONFIG_PARAMETER* params)
    : log_file_data_flags(params->get_enum(PARAM_LOG_DATA, log_data_values))
    , filebase(params->get_string(PARAM_FILEBASE))
    , flush_writes(params->get_bool(PARAM_FLUSH))
    , append(params->get_bool(PARAM_APPEND))
    , query_newline(params->get_string(PARAM_NEWLINE))
    , separator(params->get_string(PARAM_SEPARATOR))
    , user_name(params->get_string(PARAM_USER))
    , source(params->get_string(PARAM_SOURCE))
    , match(params->get_string(PARAM_MATCH))
    , exclude(params->get_string(PARAM_EXCLUDE))
{
    auto log_file_types = params->get_enum(PARAM_LOG_TYPE, log_type_values);
    write_session_log = (log_file_types & LOG_FILE_SESSION);
    write_unified_log = (log_file_types & LOG_FILE_UNIFIED);
}

QlaInstance::~QlaInstance()
{
    pcre2_code_free(m_re_match);
    pcre2_code_free(m_re_exclude);
    if (m_unified_fp != NULL)
    {
        fclose(m_unified_fp);
    }
}

QlaFilterSession::QlaFilterSession(QlaInstance& instance, MXS_SESSION* session)
    : m_instance(instance)
    , m_user(session_get_user(session))
    , m_remote(session_get_remote(session))
    , m_service(session->service->name())
    , m_ses_id(session->ses_id)
{
}

QlaFilterSession::~QlaFilterSession()
{
    pcre2_match_data_free(m_mdata);
    // File should be closed and event data freed by now
    mxb_assert(m_logfile == NULL && m_event_data.has_message == false);
}

void QlaFilterSession::close()
{
    if (m_logfile)
    {
        fclose(m_logfile);
        m_logfile = nullptr;
    }
    m_event_data.clear();
}

QlaInstance* QlaInstance::create(const std::string name, MXS_CONFIG_PARAMETER* params)
{
    bool error = false;
    QlaInstance* my_instance = NULL;

    const char* keys[] = {PARAM_MATCH, PARAM_EXCLUDE};
    pcre2_code* re_match = NULL;
    pcre2_code* re_exclude = NULL;
    uint32_t ovec_size = 0;
    int cflags = params->get_enum(PARAM_OPTIONS, option_values);
    pcre2_code** code_arr[] = {&re_match, &re_exclude};
    if (config_get_compiled_regexes(params,
                                    keys,
                                    sizeof(keys) / sizeof(char*),
                                    cflags,
                                    &ovec_size,
                                    code_arr))
    {
        // The instance is allocated before opening the file since open_log_file() takes the instance as a
        // parameter. Will be fixed (or at least cleaned) with a later refactoring of functions/methods.
        my_instance = new(std::nothrow) QlaInstance(name, params);
        if (my_instance)
        {
            my_instance->m_re_match = re_match;
            my_instance->m_re_exclude = re_exclude;
            my_instance->m_ovec_size = ovec_size;
            // Try to open the unified log file
            if (my_instance->m_settings.write_unified_log)
            {
                my_instance->m_unified_filename = my_instance->m_settings.filebase + ".unified";
                // Open the file. It is only closed at program exit.
                if (!my_instance->open_unified_logfile())
                {
                    delete my_instance;
                    my_instance = NULL;
                    error = true;
                }
            }
        }
        else
        {
            error = true;
        }
    }
    else
    {
        error = true;
    }

    if (error)
    {
        pcre2_code_free(re_match);
        pcre2_code_free(re_exclude);
    }

    return my_instance;
}

QlaFilterSession* QlaInstance::newSession(MXS_SESSION* session)
{
    auto my_session = new (std::nothrow) QlaFilterSession(*this, session);
    if (my_session)
    {
        if (!my_session->prepare())
        {
            my_session->close();
            delete my_session;
            my_session = nullptr;
        }
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

    if (m_active)
    {
        auto ovec_size = m_instance.m_ovec_size;
        if (ovec_size > 0)
        {
            m_mdata = pcre2_match_data_create(ovec_size, NULL);
            if (!m_mdata)
            {
                MXS_ERROR("pcre2_match_data_create returned NULL.");
                error = true;
            }
        }

        // Only open the session file if the corresponding mode setting is used.
        if (!error && settings.write_session_log)
        {
            m_filename = mxb::string_printf("%s.%" PRIu64, settings.filebase.c_str(), m_ses_id);
            m_logfile = m_instance.open_session_log_file(m_filename);
            if (!m_logfile)
            {
                error = true;
            }
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

void QlaInstance::diagnostics(DCB* dcb) const
{
    using mxb::string_printf;
    string output;
    if (!m_settings.source.empty())
    {
        output = string_printf("\t\tLimit logging to connections from  %s\n", m_settings.source.c_str());
    }
    if (!m_settings.user_name.empty())
    {
        output += string_printf("\t\tLimit logging to user      %s\n", m_settings.user_name.c_str());
    }
    if (!m_settings.match.empty())
    {
        output += string_printf("\t\tInclude queries that match     %s\n", m_settings.match.c_str());
    }
    if (!m_settings.exclude.empty())
    {
         output += string_printf("\t\tExclude queries that match     %s\n", m_settings.exclude.c_str());
    }

    output += string_printf("\t\tColumn separator     %s\n", m_settings.separator.c_str());
    output += string_printf("\t\tNewline replacement     %s\n", m_settings.query_newline.c_str());
    dcb_printf(dcb, "%s", output.c_str());
}

json_t* QlaInstance::diagnostics_json() const
{
    json_t* rval = json_object();
    if (!m_settings.source.empty())
    {
        json_object_set_new(rval, PARAM_SOURCE, json_string(m_settings.source.c_str()));
    }

    if (!m_settings.user_name.empty())
    {
        json_object_set_new(rval, PARAM_USER, json_string(m_settings.user_name.c_str()));
    }

    if (!m_settings.match.empty())
    {
        json_object_set_new(rval, PARAM_MATCH, json_string(m_settings.match.c_str()));
    }

    if (!m_settings.exclude.empty())
    {
        json_object_set_new(rval, PARAM_EXCLUDE, json_string(m_settings.exclude.c_str()));
    }
    json_object_set_new(rval, PARAM_SEPARATOR, json_string(m_settings.separator.c_str()));
    json_object_set_new(rval, PARAM_NEWLINE, json_string(m_settings.query_newline.c_str()));

    return rval;
}

void QlaFilterSession::check_session_log_rotation()
{
    if (check_replace_file(m_filename, &m_logfile))
    {
        // New file created, print the log header.
        string header = m_instance.generate_log_header(m_instance.m_session_data_flags);
        if (!m_instance.write_to_logfile(m_logfile, header))
        {
            MXS_ERROR(HEADER_ERROR, m_filename.c_str(), errno, mxs_strerror(errno));
            fclose(m_logfile);
            m_logfile = nullptr;
        }
    }
    // Either the old file existed or file creation failed.
}

/**
 * Write QLA log entry/entries to disk
 *
 * @params elems Log entry contents
 */
void QlaFilterSession::write_log_entries(const LogEventElems& elems)
{
    const int check_interval = 60; // Check log rotation once per minute.
    if (m_instance.m_settings.write_session_log)
    {
        if (m_file_check_timer.split().secs() > check_interval)
        {
            check_session_log_rotation();
            m_file_check_timer.restart();
        }

        if (m_logfile)
        {
            string entry = generate_log_entry(m_instance.m_session_data_flags, elems);
            write_session_log_entry(entry);
        }
    }

    if (m_instance.m_settings.write_unified_log)
    {
        string entry = generate_log_entry(m_instance.m_settings.log_file_data_flags, elems);
        m_instance.write_unified_log_entry(entry);
    }
}

int QlaFilterSession::routeQuery(GWBUF* queue)
{
    char* query = NULL;
    int query_len = 0;

    if (m_active
        && modutil_extract_SQL(queue, &query, &query_len)
        && mxs_pcre2_check_match_exclude(m_instance.m_re_match, m_instance.m_re_exclude, m_mdata,
                                         query, query_len,
                                         MXS_MODULE_NAME))
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
    return down.routeQuery(down.instance, down.session, queue);
}

int QlaFilterSession::clientReply(GWBUF* queue)
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
    return up.clientReply(up.instance, up.session, queue);
}

FILE* QlaInstance::open_session_log_file(const string& filename) const
{
    return open_log_file(m_session_data_flags, filename);
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
    // TODO: Handle log rotation here.
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

MXS_FILTER* createInstance(const char* name, MXS_CONFIG_PARAMETER* params)
{
    return QlaInstance::create(name, params);
}

MXS_FILTER_SESSION* newSession(MXS_FILTER* instance, MXS_SESSION* session)
{
    auto my_instance = static_cast<QlaInstance*>(instance);
    return my_instance->newSession(session);
}

void closeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    auto my_session = static_cast<QlaFilterSession*>(session);
    my_session->close();
}

void freeSession(MXS_FILTER* instance, MXS_FILTER_SESSION* session)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    delete my_session;
}

void setDownstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_DOWNSTREAM* downstream)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    my_session->down = *downstream;
}

void setUpstream(MXS_FILTER* instance, MXS_FILTER_SESSION* session, MXS_UPSTREAM* upstream)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    my_session->up = *upstream;
}

uint64_t getCapabilities(MXS_FILTER* instance)
{
    return RCAP_TYPE_NONE;
}

int routeQuery(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    return my_session->routeQuery(queue);
}

int clientReply(MXS_FILTER* instance, MXS_FILTER_SESSION* session, GWBUF* queue)
{
    QlaFilterSession* my_session = (QlaFilterSession*) session;
    return my_session->clientReply(queue);
}

void diagnostic(MXS_FILTER* instance, MXS_FILTER_SESSION* fsession, DCB* dcb)
{
    auto my_session = static_cast<QlaFilterSession*>(fsession);
    if (my_session)
    {
        dcb_printf(dcb,
                   "\t\tLogging to file            %s.\n",
                   my_session->m_filename.c_str());
    }
    else
    {
        auto my_instance = static_cast<QlaInstance*>(instance);
        my_instance->diagnostics(dcb);

    }
}

json_t* diagnostic_json(const MXS_FILTER* instance, const MXS_FILTER_SESSION* fsession)
{
    auto my_session = static_cast<const QlaFilterSession*>(fsession);
    if (my_session)
    {
        json_t* rval = json_object();
        json_object_set_new(rval, "session_filename", json_string(my_session->m_filename.c_str()));
        return rval;
    }
    else
    {
        auto my_instance = static_cast<const QlaInstance*>(instance);
        return my_instance->diagnostics_json();
    }
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

    static MXS_FILTER_OBJECT MyObject =
    {
        createInstance,
        newSession,
        closeSession,
        freeSession,
        setDownstream,
        setUpstream,
        routeQuery,
        clientReply,
        diagnostic,
        diagnostic_json,
        getCapabilities,
        NULL,               // No destroyInstance
    };

    static const char description[] = "A simple query logging filter";
    uint64_t capabilities = RCAP_TYPE_CONTIGUOUS_INPUT;
    static MXS_MODULE info =
    {
        MXS_MODULE_API_FILTER,
        MXS_MODULE_GA,
        MXS_FILTER_VERSION,
        description,
        "V1.1.1",
        capabilities,
        &MyObject,
        NULL,                   /* Process init. */
        NULL,                   /* Process finish. */
        NULL,                   /* Thread init. */
        NULL,                   /* Thread finish. */
        {
            {
                PARAM_MATCH,    MXS_MODULE_PARAM_REGEX
            },
            {
                PARAM_EXCLUDE,  MXS_MODULE_PARAM_REGEX
            },
            {
                PARAM_USER,     MXS_MODULE_PARAM_STRING
            },
            {
                PARAM_SOURCE,   MXS_MODULE_PARAM_STRING
            },
            {
                PARAM_FILEBASE, MXS_MODULE_PARAM_STRING,        NULL,             MXS_MODULE_OPT_REQUIRED
            },
            {
                PARAM_OPTIONS,  MXS_MODULE_PARAM_ENUM,          "ignorecase",     MXS_MODULE_OPT_NONE,
                option_values
            },
            {
                PARAM_LOG_TYPE, MXS_MODULE_PARAM_ENUM,          "session",        MXS_MODULE_OPT_NONE,
                log_type_values
            },
            {
                PARAM_LOG_DATA, MXS_MODULE_PARAM_ENUM,          LOG_DATA_DEFAULT, MXS_MODULE_OPT_NONE,
                log_data_values
            },
            {
                PARAM_NEWLINE,  MXS_MODULE_PARAM_QUOTEDSTRING,  "\" \"",          MXS_MODULE_OPT_NONE
            },
            {
                PARAM_SEPARATOR,MXS_MODULE_PARAM_QUOTEDSTRING,  ",",              MXS_MODULE_OPT_NONE
            },
            {
                PARAM_FLUSH,    MXS_MODULE_PARAM_BOOL,          "false"
            },
            {
                PARAM_APPEND,   MXS_MODULE_PARAM_BOOL,          "false"
            },
            {MXS_END_MODULE_PARAMS}
        }
    };

    return &info;
}
