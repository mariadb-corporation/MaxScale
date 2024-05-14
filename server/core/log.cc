/*
 * Copyright (c) 2016 MariaDB Corporation Ab
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

#include <maxscale/log.hh>

#include <sys/time.h>
#include <syslog.h>

#include <atomic>
#include <cinttypes>
#include <fstream>
#include <deque>
#include <charconv>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-journal.h>
#endif

#include <maxbase/alloc.hh>
#include <maxbase/jansson.hh>
#include <maxbase/log.hh>
#include <maxbase/logger.hh>
#include <maxbase/string.hh>

#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/json_api.hh>
#include <maxscale/session.hh>

#include "internal/maxscale.hh"

namespace
{

struct ThisUnit
{
    std::atomic<int> rotation_count {0};
    mxb::Regex       date{"^([0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}([.][0-9]{3})?)"};
};
ThisUnit this_unit;

const char* LOGFILE_NAME = "maxscale.log";

size_t mxs_get_context(char* buffer, size_t len)
{
    mxb_assert(len >= 20);      // Needed for "9223372036854775807"

    uint64_t session_id = session_get_current_id();

    if (session_id != 0)
    {
        auto rc = std::to_chars(buffer, buffer + len - 1, session_id);
        mxb_assert(rc.ec == std::errc {});
        len = rc.ptr - buffer;
        buffer[len] = '\0';
    }
    else
    {
        len = 0;
    }

    return len;
}

void mxs_log_in_memory(struct timeval tv, std::string_view msg)
{
    MXS_SESSION* session = session_get_current();
    if (session)
    {
        session->append_session_log(tv, msg);
    }
}

bool mxs_should_log(int priority)
{
    MXS_SESSION* session = session_get_current();
    return session && session->log_is_enabled(priority);
}
}

bool mxs_log_init(const char* ident, const char* logdir, mxb_log_target_t target)
{
    mxb::Logger::set_ident("MariaDB MaxScale");

    return mxb_log_init(ident, logdir, LOGFILE_NAME, target,
                        mxs_get_context, mxs_log_in_memory, mxs_should_log);
}

namespace
{

struct Cursors
{
    std::string current;
    std::string prev;
};

json_t* to_unix_timestamp(std::string timestamp)
{
    struct tm tp { 0 };
    strptime(timestamp.c_str(), "%Y-%m-%d %H:%M:%S", &tp);
    return json_integer(mktime(&tp));
}

#ifdef HAVE_SYSTEMD

std::string get_cursor(sd_journal* j)
{
    char* c;
    sd_journal_get_cursor(j, &c);
    std::string cur = c;
    MXB_FREE(c);
    return cur;
}

sd_journal* open_journal(const std::string& cursor)
{
    sd_journal* j = nullptr;
    int rc = sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY);

    if (rc < 0)
    {
        MXB_ERROR("Failed to open system journal: %s", mxb_strerror(-rc));
    }
    else
    {
        sd_journal_add_match(j, "_COMM=maxscale", 0);
        sd_journal_add_conjunction(j);
        sd_journal_add_match(j, "SYSLOG_IDENTIFIER=maxscale", 0);

        if (cursor.empty())
        {
            sd_journal_seek_tail(j);
        }
        else
        {
            // If the exact entry is not found, the closest available entry is returned
            sd_journal_seek_cursor(j, cursor.c_str());
        }
    }

    return j;
}

json_t* entry_to_json(sd_journal* j, const std::set<std::string>& priorities)
{
    std::map<std::string, std::string> values;
    const void* data;
    size_t length;

    while (sd_journal_enumerate_data(j, &data, &length) > 0)
    {
        std::string s((const char*)data, length);
        auto pos = s.find_first_of('=');
        auto key = s.substr(0, pos);

        if (key.front() == '_' || strncmp(key.c_str(), "SYSLOG", 6) == 0)
        {
            // Ignore auto-generated entries
            continue;
        }

        auto value = s.substr(pos + 1);

        if (!value.empty())
        {
            if (key == "PRIORITY")
            {
                // Convert the numeric priority value to the string value
                value = mxb_log_level_to_string(atoi(value.c_str()));

                if (!priorities.empty() && priorities.find(value) == priorities.end())
                {
                    return nullptr;
                }
            }

            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            values.emplace(key, value);
        }
    }

    json_t* obj = nullptr;

    // MaxScale 2.5 and older did not have the TIMESTAMP field in the log messages. If we don't find it, we
    // know this is from an older version of MaxScale and we shouldn't return it.
    if (values.find("timestamp") != values.end())
    {
        obj = json_object();
        json_object_set_new(obj, "id", json_string(get_cursor(j).c_str()));
        json_object_set_new(obj, "unix_timestamp", to_unix_timestamp(values["timestamp"]));

        for (const auto& kv : values)
        {
            json_object_set_new(obj, kv.first.c_str(), json_string(kv.second.c_str()));
        }
    }

    return obj;
}

class JournalStream
{
public:

    static std::shared_ptr<JournalStream> create(const std::string& cursor,
                                                 const std::set<std::string>& priorities)
    {
        std::shared_ptr<JournalStream> rval;

        if (auto j = open_journal(cursor))
        {
            if (cursor.empty())
            {
                // If we're streaming only future events, we must go back one event as the current cursor
                // points to the past-the-end entry in the journal. This makes sure the first new event is the
                // first one that is sent. For the normal API call the position being at past-the-end is fine
                // as it iterates the journal backwards.
                sd_journal_previous(j);
            }

            rval = std::make_shared<JournalStream>(j, priorities);
        }

        return rval;
    }

    std::string get_value()
    {
        std::string rval;

        if (sd_journal_next(m_j) > 0)
        {
            if (json_t* json = entry_to_json(m_j, m_priorities))
            {
                rval = mxb::json_dump(json, JSON_COMPACT);
                json_decref(json);
            }
        }

        return rval;
    }

    JournalStream(sd_journal* j, const std::set<std::string>& priorities)
        : m_j(j)
        , m_priorities(priorities)
    {
    }

    ~JournalStream()
    {
        sd_journal_close(m_j);
    }

private:
    sd_journal*           m_j;
    std::set<std::string> m_priorities;
};

#endif

std::pair<json_t*, Cursors> get_syslog_data(const std::string& cursor, int rows,
                                            const std::set<std::string>& priority)
{
    json_t* arr = json_array();
    Cursors cursors;

#ifdef HAVE_SYSTEMD
    if (sd_journal* j = open_journal(cursor))
    {
        for (int i = 0; i < rows && sd_journal_previous(j) > 0; i++)
        {
            if (cursors.current.empty())
            {
                cursors.current = get_cursor(j);
            }

            if (json_t* row = entry_to_json(j, priority))
            {
                json_array_insert_new(arr, 0, row);
            }
        }

        if (sd_journal_previous(j) > 0)
        {
            cursors.prev = get_cursor(j);
        }

        sd_journal_close(j);
    }
#endif

    return {arr, cursors};
}

json_t* line_to_json(std::string line, int id, const std::set<std::string>& priorities)
{
    // The timestamp is always the same size followed by three empty spaces. If high precision logging
    // is enabled, the timestamp string is four characters longer.
    mxb_assert(this_unit.date.valid());
    auto captures = this_unit.date.substr(line);

    if (captures.empty())
    {
        // Not a valid log message, ignore it
        return nullptr;
    }

    const auto& timestamp = captures[0];
    line.erase(0, timestamp.size());
    mxb::ltrim(line);

    auto prio_end = line.find_first_of(':');

    if (prio_end == std::string::npos)
    {
        return nullptr;
    }

    std::string priority = line.substr(0, prio_end);
    mxb::trim(priority);
    line.erase(0, prio_end + 1);
    mxb::ltrim(line);

    std::string session;
    std::string module;
    std::string object;
    std::string function;

    auto get_value = [&](char lp, char rp) {
            if (line.front() == lp)
            {
                line.erase(0, 1);
                std::string val = line.substr(0, line.find_first_of(rp, 1));
                line.erase(0, val.size() + 1);

                switch (line.front())
                {
                case ':':
                    function = val;
                    line.erase(0, 1);
                    break;

                case ';':
                    object = val;
                    line.erase(0, 1);
                    break;

                default:
                    if (lp == '(')
                    {
                        session = val;
                    }
                    else
                    {
                        module = val;
                    }
                    break;
                }

                mxb::ltrim(line);
            }
        };

    get_value('(', ')');
    get_value('[', ']');
    get_value('(', ')');
    get_value('(', ')');

    mxb::trim(line);

    if (!priorities.empty() && priorities.find(priority) == priorities.end())
    {
        return nullptr;
    }

    json_t* obj = json_object();
    json_object_set_new(obj, "id", json_string(std::to_string(id).c_str()));
    json_object_set_new(obj, "message", json_string(line.c_str()));
    json_object_set_new(obj, "timestamp", json_string(timestamp.c_str()));
    json_object_set_new(obj, "unix_timestamp", to_unix_timestamp(timestamp));
    json_object_set_new(obj, "priority", json_string(priority.c_str()));

    if (!session.empty())
    {
        json_object_set_new(obj, "session", json_string(session.c_str()));
    }

    if (!module.empty())
    {
        json_object_set_new(obj, "module", json_string(module.c_str()));
    }

    if (!object.empty())
    {
        json_object_set_new(obj, "object", json_string(object.c_str()));
    }

    return obj;
}

std::string next_maxlog_line(std::ifstream& file)
{
    for (std::string line; std::getline(file, line);)
    {
        auto captures = this_unit.date.substr(line);

        if (!captures.empty())
        {
            if (line.find_first_of(':', captures[0].size()) != std::string::npos)
            {
                return line;
            }
        }
    }

    return "";
}

std::pair<json_t*, Cursors> get_maxlog_data(const std::string& cursor, int rows,
                                            const std::set<std::string>& priorities)
{
    Cursors cursors;
    json_t* arr = json_array();
    std::ifstream file(mxb_log_get_filename());

    if (file.good())
    {
        std::deque<std::string> lines;
        int n = 0;

        if (!cursor.empty())
        {
            int skip = atoi(cursor.c_str());

            for (int i = 0; i < skip; i++)
            {
                if (auto line = next_maxlog_line(file); !line.empty())
                {
                    ++n;
                }
            }

            for (int i = 0; i < rows; i++)
            {
                if (auto line = next_maxlog_line(file); !line.empty())
                {
                    lines.emplace_back(std::move(line));
                    ++n;
                }
                else
                {
                    break;
                }
            }
        }
        else
        {
            auto line = next_maxlog_line(file);

            while (!line.empty())
            {
                lines.emplace_back(std::move(line));
                ++n;

                line = next_maxlog_line(file);

                if ((int)lines.size() > rows)
                {
                    lines.pop_front();
                }
            }
        }

        int row = n - lines.size();
        mxb_assert(row >= 0);
        cursors.current = std::to_string(row);

        if (row > 0)
        {
            cursors.prev = std::to_string(std::max(row - rows, 0));
        }

        for (const auto& line : lines)
        {
            if (json_t* obj = line_to_json(line, row++, priorities))
            {
                json_array_append_new(arr, obj);
            }
        }
    }

    return {arr, cursors};
}

class LogStream
{
public:

    static std::shared_ptr<LogStream> create(const std::string& cursor,
                                             const std::set<std::string>& priorities)
    {
        std::shared_ptr<LogStream> rval;
        std::ifstream file(mxb_log_get_filename());

        if (file.good())
        {
            int n = 0;

            if (cursor.empty())
            {
                while (file.ignore(std::numeric_limits<std::streamsize>::max(), '\n'))
                {
                    ++n;
                }
            }
            else
            {
                n = atoi(cursor.c_str());

                for (int i = 0; i < n; i++)
                {
                    file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                }
            }

            rval = std::make_shared<LogStream>(std::move(file), n, priorities);
        }

        return rval;
    }

    std::string get_value()
    {
        std::string rval;

        for (std::string line; std::getline(m_file, line);)
        {
            if (json_t* obj = line_to_json(line, m_lineno++, m_priorities))
            {
                rval = mxb::json_dump(obj, JSON_COMPACT);
                json_decref(obj);
                break;
            }
        }

        // Clear the eof flag so that new lines are read
        m_file.clear();

        return rval;
    }

    LogStream(std::ifstream&& file, int lineno, const std::set<std::string>& priorities)
        : m_file(std::move(file))
        , m_lineno(lineno)
        , m_priorities(priorities)
    {
    }

private:
    std::ifstream         m_file;
    int                   m_lineno = 0;
    std::set<std::string> m_priorities;
};

json_t* get_log_priorities()
{
    json_t* arr = json_array();

    if (mxb_log_is_priority_enabled(LOG_ALERT))
    {
        json_array_append_new(arr, json_string("alert"));
    }

    if (mxb_log_is_priority_enabled(LOG_ERR))
    {
        json_array_append_new(arr, json_string("error"));
    }

    if (mxb_log_is_priority_enabled(LOG_WARNING))
    {
        json_array_append_new(arr, json_string("warning"));
    }

    if (mxb_log_is_priority_enabled(LOG_NOTICE))
    {
        json_array_append_new(arr, json_string("notice"));
    }

    if (mxb_log_is_priority_enabled(LOG_INFO))
    {
        json_array_append_new(arr, json_string("info"));
    }

    if (mxb_log_is_priority_enabled(LOG_DEBUG))
    {
        json_array_append_new(arr, json_string("debug"));
    }

    return arr;
}
}

json_t* mxs_logs_to_json(const char* host)
{
    std::unordered_set<std::string> log_params = {
        "maxlog",     "syslog",    "log_info",       "log_warning",
        "log_notice", "log_debug", "log_throttling", "ms_timestamp"
    };

    json_t* params = mxs::Config::get().to_json();
    void* ptr;
    const char* key;
    json_t* value;

    // Remove other parameters to appear more backwards compatible
    json_object_foreach_safe(params, ptr, key, value)
    {
        if (log_params.count(key) == 0)
        {
            json_object_del(params, key);
        }
    }

    json_t* attr = json_object();
    json_object_set_new(attr, CN_PARAMETERS, params);
    json_object_set_new(attr, "log_file", json_string(mxb_log_get_filename()));
    json_object_set_new(attr, "log_priorities", get_log_priorities());

    json_t* data = json_object();
    json_object_set_new(data, CN_ATTRIBUTES, attr);
    json_object_set_new(data, CN_ID, json_string("logs"));
    json_object_set_new(data, CN_TYPE, json_string("logs"));

    return mxs_json_resource(host, MXS_JSON_API_LOGS, data);
}

void create_pagination_links(json_t* rval,
                             int rows,
                             const std::set<std::string>& priorities,
                             const Cursors& cursors)
{
    // Create pagination links
    json_t* links = json_object_get(rval, CN_LINKS);
    std::string base = json_string_value(json_object_get(links, "self"));
    std::string prio;

    if (!priorities.empty())
    {
        prio = "&priority=" + mxb::join(priorities);
    }

    const std::string LB = "%5B";   // Percent-encoded [
    const std::string RB = "%5D";   // Percent-encoded ]

    if (!cursors.prev.empty())
    {
        auto prev = base + "?page" + LB + "cursor" + RB + "=" + cursors.prev
            + "&page" + LB + "size" + RB + "=" + std::to_string(rows) + prio;
        json_object_set_new(links, "prev", json_string(prev.c_str()));
    }

    if (!cursors.current.empty())
    {
        auto self = base + "?page" + LB + "cursor" + RB + "=" + cursors.current
            + "&page" + LB + "size" + RB + "=" + std::to_string(rows) + prio;
        json_object_set_new(links, "self", json_string(self.c_str()));
    }

    auto last = base + "?page" + LB + "size" + RB + "=" + std::to_string(rows) + prio;
    json_object_set_new(links,
                        "last",
                        json_string(last.c_str()));
}

std::tuple<json_t*, Cursors, const char*> get_log_data(const std::string& cursor, int rows,
                                                       const std::set<std::string>& priorities)
{
    const auto& cnf = mxs::Config::get();
    Cursors cursors;
    json_t* log = nullptr;
    const char* log_source = nullptr;

    if (cnf.syslog.get())
    {
        std::tie(log, cursors) = get_syslog_data(cursor, rows, priorities);
        log_source = "syslog";
    }
    else if (cnf.maxlog.get())
    {
        std::tie(log, cursors) = get_maxlog_data(cursor, rows, priorities);
        log_source = "maxlog";
    }

    return {log, cursors, log_source};
}

json_t* mxs_log_data_to_json(const char* host, const std::string& cursor, int rows,
                             const std::set<std::string>& priorities)
{
    json_t* attr = json_object();
    auto [log, cursors, log_source] = get_log_data(cursor, rows, priorities);

    if (log_source && log)
    {
        json_object_set_new(attr, "log_source", json_string(log_source));
        json_object_set_new(attr, "log", log);
    }

    json_t* data = json_object();
    json_object_set_new(data, CN_ATTRIBUTES, attr);
    json_object_set_new(data, CN_ID, json_string("log_data"));
    json_object_set_new(data, CN_TYPE, json_string("log_data"));

    json_t* rval = mxs_json_resource(host, MXS_JSON_API_LOG_DATA, data);
    create_pagination_links(rval, rows, priorities, cursors);
    return rval;
}

json_t* mxs_log_entries_to_json(const char* host, const std::string& cursor, int rows,
                                const std::set<std::string>& priorities)
{
    auto [log, cursors, log_source] = get_log_data(cursor, rows, priorities);

    if (log_source && log)
    {
        // The log data is returned as a JSON array. We need to modify it to be a proper JSON API resource
        // collection by moving a few things around.
        size_t i;
        json_t* v;

        json_array_foreach(log, i, v)
        {
            json_t* o = json_object();
            json_object_set_new(o, CN_TYPE, json_string("log_entry"));
            json_object_set(o, CN_ID, json_object_get(v, CN_ID));
            json_object_set(o, CN_ATTRIBUTES, v);
            json_object_set_new(v, "log_source", json_string(log_source));
            json_object_del(v, CN_ID);
            json_array_set_new(log, i, o);
        }
    }
    else
    {
        log = json_array();
    }

    json_t* rval = mxs_json_resource(host, MXS_JSON_API_LOG_ENTRIES, log);
    create_pagination_links(rval, rows, priorities, cursors);
    return rval;
}

std::function<std::string()> mxs_logs_stream(const std::string& cursor,
                                             const std::set<std::string>& priorities)
{
    const auto& cnf = mxs::Config::get();

    if (cnf.syslog.get())
    {
#ifdef HAVE_SYSTEMD
        if (auto stream = JournalStream::create(cursor, priorities))
        {
            return [stream]() {
                       return stream->get_value();
                   };
        }
#else
	MXB_ERROR("MaxScale was built without SystemD support.");
#endif
    }
    else if (cnf.maxlog.get())
    {
        if (auto stream = LogStream::create(cursor, priorities))
        {
            return [stream]() {
                       return stream->get_value();
                   };
        }
    }
    else
    {
        MXB_ERROR("Neither `syslog` or `maxlog` is enabled, cannot stream logs.");
    }

    return {};
}

bool mxs_log_rotate()
{
    bool rotated = mxb_log_rotate();
    if (rotated)
    {
        this_unit.rotation_count.fetch_add(1, std::memory_order_relaxed);
        maxscale_log_info_blurb(LogBlurbAction::LOG_ROTATION);
    }
    return rotated;
}

int mxs_get_log_rotation_count()
{
    return this_unit.rotation_count.load(std::memory_order_relaxed);
}
