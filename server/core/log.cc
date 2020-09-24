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

#include <maxscale/log.hh>

#include <sys/time.h>
#include <syslog.h>

#include <atomic>
#include <cinttypes>

#ifdef HAVE_SYSTEMD
#include <systemd/sd-journal.h>
#endif

#include <maxbase/log.hh>
#include <maxbase/logger.hh>

#include <maxscale/cn_strings.hh>
#include <maxscale/config.hh>
#include <maxscale/jansson.hh>
#include <maxscale/json_api.hh>
#include <maxscale/session.hh>

namespace
{

struct ThisUnit
{
    std::atomic<int> rotation_count {0};
};
ThisUnit this_unit;

const char* LOGFILE_NAME = "maxscale.log";

size_t mxs_get_context(char* buffer, size_t len)
{
    mxb_assert(len >= 20);      // Needed for "9223372036854775807"

    uint64_t session_id = session_get_current_id();

    if (session_id != 0)
    {
        len = snprintf(buffer, len, "%" PRIu64, session_id);
    }
    else
    {
        len = 0;
    }

    return len;
}

void mxs_log_in_memory(const char* msg, size_t len)
{
    MXS_SESSION* session = session_get_current();
    if (session)
    {
        session_append_log(session, msg);
    }
}
}

bool mxs_log_init(const char* ident, const char* logdir, mxs_log_target_t target)
{
    mxb::Logger::set_ident("MariaDB MaxScale");

    return mxb_log_init(ident, logdir, LOGFILE_NAME, target, mxs_get_context, mxs_log_in_memory);
}

namespace
{

struct Cursors
{
    std::string current;
    std::string prev;
};

#ifdef HAVE_SYSTEMD

std::string get_cursor(sd_journal* j)
{
    char* c;
    sd_journal_get_cursor(j, &c);
    std::string cur = c;
    MXS_FREE(c);
    return cur;
}

sd_journal* open_journal(const std::string& cursor)
{
    sd_journal* j = nullptr;
    int rc = sd_journal_open(&j, SD_JOURNAL_LOCAL_ONLY);

    if (rc < 0)
    {
        MXS_ERROR("Failed to open system journal: %s", mxs_strerror(-rc));
    }
    else
    {
        sd_journal_add_match(j, "_COMM=maxscale", 0);

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

json_t* entry_to_json(sd_journal* j)
{
    json_t* obj = json_object();
    json_object_set_new(obj, "id", json_string(get_cursor(j).c_str()));

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
            }

            std::transform(key.begin(), key.end(), key.begin(), ::tolower);
            json_object_set_new(obj, key.c_str(), json_string(value.c_str()));
        }
    }

    return obj;
}

class JournalStream
{
public:

    static std::shared_ptr<JournalStream> create(const std::string& cursor)
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

            rval = std::make_shared<JournalStream>(j);
        }

        return rval;
    }

    std::string get_value()
    {
        std::string rval;

        if (sd_journal_next(m_j) > 0)
        {
            json_t* json = entry_to_json(m_j);
            rval = mxs::json_dump(json, JSON_COMPACT);
            json_decref(json);
        }

        return rval;
    }

    JournalStream(sd_journal* j)
        : m_j(j)
    {
    }

    ~JournalStream()
    {
        sd_journal_close(m_j);
    }

private:
    sd_journal* m_j;
};

#endif

std::pair<json_t*, Cursors> get_syslog_data(const std::string& cursor, int rows)
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

            json_array_append_new(arr, entry_to_json(j));
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

json_t* mxs_logs_to_json(const char* host, const std::string& cursor, int rows)
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

    const auto& cnf = mxs::Config::get();

    Cursors cursors;
    json_t* log = nullptr;

    if (cnf.syslog.get())
    {
        std::tie(log, cursors) = get_syslog_data(cursor, rows);
    }

    json_object_set_new(attr, "log", log);

    json_t* data = json_object();
    json_object_set_new(data, CN_ATTRIBUTES, attr);
    json_object_set_new(data, CN_ID, json_string("logs"));
    json_object_set_new(data, CN_TYPE, json_string("logs"));

    json_t* rval = mxs_json_resource(host, MXS_JSON_API_LOGS, data);

    // Create pagination links
    json_t* links = json_object_get(rval, CN_LINKS);
    std::string base = json_string_value(json_object_get(links, "self"));

    if (!cursors.prev.empty())
    {
        auto prev = base + "?page[cursor]=" + cursors.prev + "&page[size]=" + std::to_string(rows);
        json_object_set_new(links, "prev", json_string(prev.c_str()));
    }

    if (!cursors.current.empty())
    {
        auto self = base + "?page[cursor]=" + cursors.current + "&page[size]=" + std::to_string(rows);
        json_object_set_new(links, "self", json_string(self.c_str()));
    }

    auto last = base + "?page[size]=" + std::to_string(rows);
    json_object_set_new(links, "last", json_string(last.c_str()));

    return rval;
}

std::function<std::string()> mxs_logs_stream(const std::string& cursor)
{
    const auto& cnf = mxs::Config::get();

    if (cnf.syslog.get())
    {
        if (auto stream = JournalStream::create(cursor))
        {
            return [stream]() {
                       return stream->get_value();
                   };
        }
    }
    else if (cnf.maxlog.get())
    {
        MXS_ERROR("Not yet implemented");
    }
    else
    {
        MXS_ERROR("Neither `syslog` or `maxlog` is enabled, cannot stream logs.");
    }

    return {};
}

bool mxs_log_rotate()
{
    bool rotated = mxb_log_rotate();
    if (rotated)
    {
        this_unit.rotation_count.fetch_add(1, std::memory_order_relaxed);
    }
    return rotated;
}

int mxs_get_log_rotation_count()
{
    return this_unit.rotation_count.load(std::memory_order_relaxed);
}
