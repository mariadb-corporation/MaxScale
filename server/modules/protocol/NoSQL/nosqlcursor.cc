/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "nosqlcursor.hh"
#include <sstream>
#include <maxbase/worker.hh>
#include <maxscale/mainworker.hh>
#include "nosqlcommand.hh"

using std::set;
using std::string;
using std::ostringstream;
using std::vector;

namespace
{

using namespace nosql;

class ThisUnit
{
public:
    ThisUnit()
    {
    }

    ThisUnit(const ThisUnit&) = delete;
    ThisUnit& operator=(const ThisUnit&) = delete;

    int64_t next_id()
    {
        // TODO: Later we probably want to create a random id, not a guessable one.
        return ++m_id;
    }

    void put_cursor(std::unique_ptr<NoSQLCursor> sCursor)
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        CursorsById& cursors = m_collection_cursors[sCursor->ns()];

        mxb_assert(cursors.find(sCursor->id()) == cursors.end());

        cursors.insert(std::make_pair(sCursor->id(), std::move(sCursor)));
    }

    std::unique_ptr<NoSQLCursor> get_cursor(const std::string& collection, int64_t id)
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        auto it = m_collection_cursors.find(collection);

        if (it == m_collection_cursors.end())
        {
            throw_cursor_not_found(id);
        }

        CursorsById& cursors = it->second;

        auto jt = cursors.find(id);

        if (jt == cursors.end())
        {
            throw_cursor_not_found(id);
        }

        auto sCursor = std::move(jt->second);

        cursors.erase(jt);

        if (cursors.size() == 0)
        {
            m_collection_cursors.erase(it);
        }

        return sCursor;
    }

    std::set<int64_t> kill_cursors(const std::string& collection, const std::vector<int64_t>& ids)
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        set<int64_t> removed;

        auto it = m_collection_cursors.find(collection);

        if (it != m_collection_cursors.end())
        {
            CursorsById& cursors = it->second;

            for (auto id : ids)
            {
                auto jt = cursors.find(id);

                if (jt != cursors.end())
                {
                    cursors.erase(jt);
                    removed.insert(id);
                }
            }
        }

        return removed;
    }

    void kill_idle_cursors(const mxb::TimePoint& now, const std::chrono::seconds& timeout)
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        for (auto& kv : m_collection_cursors)
        {
            CursorsById& cursors = kv.second;

            auto it = cursors.begin();

            while (it != cursors.end())
            {
                auto& sCursor = it->second;

                auto idle = now - sCursor->last_use();

                if (idle > timeout)
                {
                    it = cursors.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }

private:
    void throw_cursor_not_found(int64_t id)
    {
        ostringstream ss;
        ss << "cursor id " << id << " not found";
        throw nosql::SoftError(ss.str(), nosql::error::CURSOR_NOT_FOUND);
    }

    using CursorsById = std::unordered_map<int64_t, std::unique_ptr<NoSQLCursor>>;
    using CollectionCursors = std::unordered_map<std::string, CursorsById>;

    std::atomic<int64_t> m_id;
    std::mutex           m_mutex;
    CollectionCursors    m_collection_cursors;
} this_unit;

// If bit 63 is 0 and bit 62 a 1, then the value is interpreted as a 'Long'.
const int64_t BSON_LONG_BIT = (int64_t(1) << 62);

string create_leaf_entry(const string& extraction, const std::string& value)
{
    mxb_assert(extraction.find('.') == string::npos);

    return "\"" + extraction + "\": " + value;
}

string create_nested_entry(const string& extraction, const std::string& value)
{
    string entry;
    auto i = extraction.find('.');

    if (i == string::npos)
    {
        entry = "{ "  + create_leaf_entry(extraction, value) + " }";
    }
    else
    {
        auto head = extraction.substr(0, i);
        auto tail = extraction.substr(i + 1);

        entry = "{ \"" + head + "\": " + create_nested_entry(tail, value) + "}";
    }

    return entry;
}

string create_entry(const string& extraction, const std::string& value)
{
    string entry;
    auto i = extraction.find('.');

    if (i == string::npos)
    {
        entry = create_leaf_entry(extraction, value);
    }
    else
    {
        auto head = extraction.substr(0, i);
        auto tail = extraction.substr(i + 1);

        entry = "\"" + head + "\": " + create_nested_entry(tail, value);;
    }

    return entry;
}

}

namespace nosql
{

NoSQLCursor::NoSQLCursor(const std::string& ns)
    : m_ns(ns)
    , m_id(0)
    , m_exhausted(false)
{
    touch();
}

NoSQLCursor::NoSQLCursor(const std::string& ns,
                         const vector<string>& extractions,
                         mxs::Buffer&& mariadb_response)
    : m_ns(ns)
    , m_id(this_unit.next_id() | BSON_LONG_BIT)
    , m_exhausted(false)
    , m_extractions(std::move(extractions))
    , m_mariadb_response(mariadb_response)
    , m_pBuffer(gwbuf_link_data(m_mariadb_response.get()))
    , m_used(mxb::Worker::get_current()->epoll_tick_now())
{
    initialize();
    touch();
}

//static
std::unique_ptr<NoSQLCursor> NoSQLCursor::create(const std::string& ns)
{
    return std::unique_ptr<NoSQLCursor>(new NoSQLCursor(ns));
}

//static
std::unique_ptr<NoSQLCursor> NoSQLCursor::create(const std::string& ns,
                                                 const std::vector<std::string>& extractions,
                                                 mxs::Buffer&& mariadb_response)
{
    return std::unique_ptr<NoSQLCursor>(new NoSQLCursor(ns, extractions, std::move(mariadb_response)));
}

//static
std::unique_ptr<NoSQLCursor> NoSQLCursor::get(const std::string& collection, int64_t id)
{
    return std::move(this_unit.get_cursor(collection, id));
}

//static
void NoSQLCursor::put(std::unique_ptr<NoSQLCursor> sCursor)
{
    this_unit.put_cursor(std::move(sCursor));
}

//static
std::set<int64_t> NoSQLCursor::kill(const std::string& collection, const std::vector<int64_t>& ids)
{
    return this_unit.kill_cursors(collection, ids);
}

//static
void NoSQLCursor::kill_idle(const mxb::TimePoint& now, const std::chrono::seconds& timeout)
{
    this_unit.kill_idle_cursors(now, timeout);
}

//static
void NoSQLCursor::start_purging_idle_cursors(const std::chrono::seconds& cursor_timeout)
{
    // This should be called at startup, so we must be on MainWorker.
    mxb_assert(mxs::MainWorker::is_main_worker());

    auto* pMain = mxs::MainWorker::get();

    // The time between checks whether cursors need to be killed is defined
    // as 1/10 of the cursor timeout, but at least 1 second.
    std::chrono::milliseconds wait_timeout = cursor_timeout;
    wait_timeout /= 10;

    if (wait_timeout == std::chrono::milliseconds(0))
    {
        wait_timeout = std::chrono::milliseconds(1000);
    }

    // We don't ever want to cancel this explicitly so the delayed call will
    // be cancelled when MainWorker is destructed.
    pMain->delayed_call(wait_timeout, [pMain, cursor_timeout](mxb::Worker::Call::action_t action) {
            if (action == mxb::Worker::Call::EXECUTE)
            {
                kill_idle(pMain->epoll_tick_now(), cursor_timeout);
            }

            return true; // Call again
        });
}

void NoSQLCursor::create_first_batch(bsoncxx::builder::basic::document& doc,
                                     int32_t nBatch,
                                     bool single_batch)
{
    create_batch(doc, key::FIRSTBATCH, nBatch, single_batch);
}

void NoSQLCursor::create_next_batch(bsoncxx::builder::basic::document& doc, int32_t nBatch)
{
    create_batch(doc, key::NEXTBATCH, nBatch, false);
}

//static
void NoSQLCursor::create_first_batch(bsoncxx::builder::basic::document& doc,
                                     const std::string& ns)
{
    ArrayBuilder batch;

    int64_t id = 0;

    DocumentBuilder cursor;
    cursor.append(kvp("firstBatch", batch.extract()));
    cursor.append(kvp("id", id));
    cursor.append(kvp("ns", ns));

    doc.append(kvp("cursor", cursor.extract()));
    doc.append(kvp("ok", 1));
}

void NoSQLCursor::create_batch(bsoncxx::builder::basic::document& doc,
                               const string& which_batch,
                               int32_t nBatch,
                               bool single_batch)
{
    mxb_assert(!m_exhausted);

    ArrayBuilder batch;

    int64_t id = 0;

    if (m_pBuffer)
    {
        if (create_batch(batch, nBatch) == Result::PARTIAL)
        {
            id = m_id;
        }
    }
    else
    {
        m_exhausted = true;
    }

    if (single_batch)
    {
        m_exhausted = true;
        id = 0;
    }

    DocumentBuilder cursor;
    cursor.append(kvp(which_batch, batch.extract()));
    cursor.append(kvp("id", id));
    cursor.append(kvp("ns", m_ns));

    doc.append(kvp("cursor", cursor.extract()));
    doc.append(kvp("ok", 1));

    touch();
}

NoSQLCursor::Result NoSQLCursor::create_batch(bsoncxx::builder::basic::array& batch, int32_t nBatch)
{
    int n = 0;
    while (n < nBatch && ComResponse(m_pBuffer).type() != ComResponse::EOF_PACKET) // m_pBuffer not advanced
    {
        ++n;
        // m_pBuffer cannot be advanced before we know whether the object will fit.
        auto pBuffer = m_pBuffer;
        CQRTextResultsetRow row(&pBuffer, m_types); // Advances pBuffer

        auto it = row.begin();

        string json;

        if (m_extractions.empty())
        {
            const auto& value = *it++;
            mxb_assert(it == row.end());
            // The value is now a JSON object.
            json = value.as_string().to_string();
        }
        else
        {
            auto jt = m_extractions.begin();

            bool first = true;
            json += "{";
            for (; it != row.end(); ++it, ++jt)
            {
                if (first)
                {
                    first = false;
                }
                else
                {
                    json += ", ";
                }

                const auto& value = *it;
                auto extraction = *jt;

                json += create_entry(extraction, value.as_string().to_string());
            }
            json += "}";
        }

        try
        {
            auto doc = bsoncxx::from_json(json);

            if (batch.view().length() + doc.view().length() > protocol::MAX_MSG_SIZE)
            {
                // TODO: Don't discard the converted doc, but store it somewhere for
                // TODO: the next batch.
                break;
            }

            batch.append(doc);
            m_pBuffer = pBuffer;
        }
        catch (const std::exception& x)
        {
            ostringstream ss;
            ss << "Could not convert assumed JSON data to BSON: " << x.what();
            MXB_ERROR("%s. Data: %s", ss.str().c_str(), json.c_str());
            throw SoftError(ss.str(), error::COMMAND_FAILED);
        }
    }

    bool at_end = (ComResponse(m_pBuffer).type() == ComResponse::EOF_PACKET);

    if (at_end)
    {
        ComResponse response(&m_pBuffer);
        m_exhausted = true;
    }

    return at_end ? Result::COMPLETE : Result::PARTIAL;
}

void NoSQLCursor::initialize()
{
    ComQueryResponse cqr(&m_pBuffer);

    auto nFields = cqr.nFields();

    // If there are no extractions, then we SELECTed the entire document and there should
    // be just one field (the JSON document). Otherwise there should be as many fields
    // (JSON_EXTRACT(doc, '$...')) as there are extractions.
    mxb_assert((m_extractions.empty() && nFields == 1) || (m_extractions.size() == nFields));

    for (size_t i = 0; i < nFields; ++i)
    {
        // ... and then as many column definitions.
        ComQueryResponse::ColumnDef column_def(&m_pBuffer);

        m_names.push_back(column_def.name().to_string());
        m_types.push_back(column_def.type());
    }

    // The there should be an EOF packet, which should be bypassed.
    ComResponse eof(&m_pBuffer);
    mxb_assert(eof.type() == ComResponse::EOF_PACKET);

    // Now m_pBuffer points at the beginning of rows.
}

void NoSQLCursor::touch()
{
    m_used = mxb::Worker::get_current()->epoll_tick_now();
}

}
