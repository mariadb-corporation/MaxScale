/*
 * Copyright (c) 2019 MariaDB Corporation Ab
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

#define MXS_MODULE_NAME "qlafilter"

#include <maxscale/ccdefs.hh>

#include <string>
#include <maxscale/config.hh>
#include <maxscale/filter.hh>
#include <maxscale/pcre2.h>


/* Date string buffer size */
#define QLA_DATE_BUFFER_SIZE 20

class QlaFilterSession;

/**
 * Helper struct for holding data before it's written to file.
 */
class LogEventData
{
public:
    LogEventData(const LogEventData&) = delete;
    LogEventData& operator=(const LogEventData&) = default;
    LogEventData() = default;

    ~LogEventData()
    {
        mxb_assert(query_clone == NULL);
    }

    /**
     * Resets event data.
     *
     * @param event Event to reset
     */
    void clear()
    {
        gwbuf_free(query_clone);
        *this = LogEventData();
    }

    bool     has_message {false};               // Does message data exist?
    GWBUF*   query_clone {nullptr};             // Clone of the query buffer.
    char     query_date[QLA_DATE_BUFFER_SIZE];  // Text representation of date.
    timespec begin_time {0, 0};                 // Timer value at the moment of receiving query.
};

/**
 * A instance structure, the assumption is that the option passed
 * to the filter is simply a base for the filename to which the queries
 * are logged.
 *
 * To this base a session number is attached such that each session will
 * have a unique name.
 */
class QlaInstance : public MXS_FILTER
{
public:
    QlaInstance(const QlaInstance&) = delete;
    QlaInstance& operator=(const QlaInstance&) = delete;

    QlaInstance(const std::string& name, MXS_CONFIG_PARAMETER* params);
    ~QlaInstance();

    /**
     * Associate a new session with this instance of the filter. Creates a session-specific logfile.
     *
     * @param session   The generic session
     * @return          Router session on null on error
     */
    QlaFilterSession* newSession(MXS_SESSION* session);

    /**
     * Create an instance of the filter for a particular service within MaxScale.
     *
     * @param name      The name of the instance (as defined in the config file)
     * @param params    The array of name/value pair parameters for the filter
     * @return          The new filter instance, or NULL on error
     */
    static QlaInstance* create(const std::string name, MXS_CONFIG_PARAMETER* params);

    /**
     * Read contents of unified log file and save to json object.
     *
     * @param start First line to output
     * @param end Last line to output
     * @param output Where to save read lines
     * @return True if file was opened
     */
    bool read_to_json(int start, int end, json_t** output) const;

    void diagnostics(DCB* dcb) const;
    json_t* diagnostics_json() const;
    FILE* open_log_file(uint32_t, const char*);

    const std::string m_name;   /* Filter definition name */

    std::string m_unified_filename;      /* Filename of the unified log file */
    FILE*       m_unified_fp {nullptr};  /* Unified log file. The pointer needs to be shared here
                                          * to avoid garbled printing. */

    pcre2_code* m_re_match {nullptr};    /* Compiled regex text */
    pcre2_code* m_re_exclude {nullptr};  /* Compiled regex nomatch text */
    uint32_t    m_ovec_size {0};         /* PCRE2 match data ovector size */

    bool        m_write_warning_given {false}; /* Avoid repeatedly printing some errors/warnings. */

    class Settings
    {
    public:
        Settings(MXS_CONFIG_PARAMETER* params);

        bool        write_unified_log {false};
        bool        write_session_log {false};
        uint32_t    log_file_data_flags {0};   /* What data is saved to the files */
        std::string filebase;                  /* The filename base */
        bool        flush_writes {false};      /* Flush log file after every write? */
        bool        append {false};            /* Open files in append-mode? */
        std::string query_newline;             /* Character(s) used to replace a newline within a query */
        std::string separator;                 /*  Character(s) used to separate elements */
        std::string user_name;                 /* The user name to filter on */
        std::string source;                    /* The source of the client connection to filter on */
        std::string match;                     /* Optional text to match against */
        std::string exclude;                   /* Optional text to match against for exclusion */
    };

    Settings m_settings;
};

/* The session structure for this QLA filter. */
class QlaFilterSession : public MXS_FILTER_SESSION
{
public:
    QlaFilterSession(const QlaFilterSession&);
    QlaFilterSession& operator=(const QlaFilterSession&);
    QlaFilterSession(QlaInstance& instance, MXS_SESSION* session);
    ~QlaFilterSession();

    /**
     * Prepares a session for routing. Checks if username and/or host match and opens the log file.
     *
     * @return True on success. If false is returned, the session should be closed and deleted.
     */
    bool prepare();

    /**
     * Route a query.
     *
     * @param query
     * @return 0 on success
     */
    int routeQuery(GWBUF* query);

    /**
     * Route a reply from backend. Required for measuring and printing query execution time.
     *
     * @param reply Reply from server
     * @return 0 on success
     */
    int clientReply(GWBUF* reply);

    /**
     * Close a session with the filter. Close the file descriptor and reset event info.
     */
    void close();

    QlaInstance&      m_instance;

    const std::string m_user;         /* Client username */
    const std::string m_remote;       /* Client address */
    const std::string m_service;      /* The service name this filter is attached to. */
    const uint64_t    m_ses_id {0};   /* The session this filter session serves. */

    bool              m_active {false};     /* Is session active? */
    pcre2_match_data* m_mdata {nullptr};    /* Regex match data */

    std::string       m_filename;           /* The session-specific log file name */
    FILE*             m_logfile {nullptr};  /* The session-specific log file */

    LogEventData      m_event_data; /* Information about the latest event, used if logging execution time. */

    MXS_UPSTREAM   up;
    MXS_DOWNSTREAM down;

    void write_log_entries(const char* date_string, const char* query, int querylen, int elapsed_ms);
    int write_log_entry(FILE* logfile, uint32_t data_flags, const char* time_string,
                            const char* sql_string, size_t sql_str_len, int elapsed_ms);
};
