/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <set>
#include <string>
#include <vector>
#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <mysql.h>
#include <maxbase/json.hh>
#include <maxbase/stopwatch.hh>
#include <maxbase/worker.hh>
#include <maxscale/buffer.hh>

namespace nosql
{

// TODO: This should not be here, but putting it somewhere more appropriate
// TODO: has to wait for a general restructuring of headers.
struct Extraction
{
    Extraction() = default;

    Extraction(std::string_view s)
        : name(s)
    {
    }

    Extraction(std::string_view s, bsoncxx::document::element e)
        : name(s)
        , element(e)
    {
    }

    std::string                               name;
    std::optional<bsoncxx::document::element> element;
};

class NoSQLCursor
{
public:
    virtual ~NoSQLCursor();

    static std::unique_ptr<NoSQLCursor> get(const std::string& collection, int64_t id);
    static void put(std::unique_ptr<NoSQLCursor> sCursor);
    static std::set<int64_t> kill(const std::string& collection, const std::vector<int64_t>& ids);
    static std::set<int64_t> kill(const std::vector<int64_t>& ids);
    static void kill_idle(const mxb::TimePoint& now, const std::chrono::seconds& timeout);
    static void purge(const std::string& collection);

    static void start_purging_idle_cursors(const std::chrono::seconds& cursor_timeout);

    const std::string& ns() const
    {
        return m_ns;
    }

    int64_t id() const
    {
        return m_id;
    }

    bool exhausted() const
    {
        return m_exhausted;
    }

    int32_t position() const
    {
        return m_position;
    }

    const mxb::TimePoint& last_use() const
    {
        return m_used;
    }

    virtual int32_t nRemaining() const = 0;

    static void create_empty_first_batch(bsoncxx::builder::basic::document& doc,
                                         const std::string& ns);


    virtual void create_first_batch(mxb::Worker& worker,
                                    bsoncxx::builder::basic::document& doc,
                                    int32_t nBatch,
                                    bool single_batch) = 0;

    virtual void create_next_batch(mxb::Worker& worker,
                                   bsoncxx::builder::basic::document& doc,
                                   int32_t nBatch) = 0;

    virtual void create_batch(mxb::Worker& worker,
                              int32_t nBatch,
                              bool single_batch,
                              size_t* pnSize_of_documents,
                              std::vector<bsoncxx::document::value>* pDocuments) = 0;

protected:
    NoSQLCursor(const std::string& ns, int64_t id);

    void touch(mxb::Worker& worker);

    const std::string m_ns;
    const int64_t     m_id;
    int32_t           m_position { 0 };
    bool              m_exhausted { false };
    mxb::TimePoint    m_used;
};


class NoSQLCursorResultSet : public NoSQLCursor
{
public:
    NoSQLCursorResultSet(const NoSQLCursorResultSet& rhs) = delete;

    static std::unique_ptr<NoSQLCursor> create(const std::string& ns);

    static std::unique_ptr<NoSQLCursor> create(const std::string& ns,
                                               const std::vector<Extraction>& extractions,
                                               GWBUF&& mariadb_response);


    void create_first_batch(mxb::Worker& worker,
                            bsoncxx::builder::basic::document& doc,
                            int32_t nBatch,
                            bool single_batch) override;
    void create_next_batch(mxb::Worker& worker,
                           bsoncxx::builder::basic::document& doc,
                           int32_t nBatch) override;

    void create_batch(mxb::Worker& worker,
                      int32_t nBatch,
                      bool single_batch,
                      size_t* pnSize_of_documents,
                      std::vector<bsoncxx::document::value>* pDocuments) override;

    int32_t nRemaining() const override;

private:
    NoSQLCursorResultSet(const std::string& ns);

    NoSQLCursorResultSet(const std::string& ns,
                         const std::vector<Extraction>& extractions,
                         GWBUF&& mariadb_response);

    void initialize();

    enum class Result
    {
        PARTIAL,
        COMPLETE
    };

    void create_batch(mxb::Worker& worker,
                      bsoncxx::builder::basic::document& doc,
                      const std::string& which_batch,
                      int32_t nBatch,
                      bool single_batch);

    void create_batch(mxb::Worker& worker,
                      int32_t nBatch,
                      bool single_batch);


    Result create_batch(std::function<bool(bsoncxx::document::value&& doc)> append, int32_t nBatch);

    std::vector<Extraction>       m_extractions;
    GWBUF                         m_mariadb_response;
    uint8_t*                      m_pBuffer { nullptr };
    size_t                        m_nBuffer { 0 };
    std::vector<std::string>      m_names;
    std::vector<enum_field_types> m_types;
};


class NoSQLCursorJson : public NoSQLCursor
{
public:
    NoSQLCursorJson(const NoSQLCursorJson& rhs) = delete;

    static std::unique_ptr<NoSQLCursor> create(const std::string& ns, std::vector<mxb::Json>&& docs);


    void create_first_batch(mxb::Worker& worker,
                            bsoncxx::builder::basic::document& doc,
                            int32_t nBatch,
                            bool single_batch) override;
    void create_next_batch(mxb::Worker& worker,
                           bsoncxx::builder::basic::document& doc,
                           int32_t nBatch) override;

    void create_batch(mxb::Worker& worker,
                      int32_t nBatch,
                      bool single_batch,
                      size_t* pnSize_of_documents,
                      std::vector<bsoncxx::document::value>* pDocuments) override;

    int32_t nRemaining() const override;

private:
    NoSQLCursorJson(const std::string& ns, std::vector<mxb::Json>&& docs);

    enum class Result
    {
        PARTIAL,
        COMPLETE
    };

    void create_batch(mxb::Worker& worker,
                      bsoncxx::builder::basic::document& doc,
                      const std::string& which_batch,
                      int32_t nBatch,
                      bool single_batch);

    void create_batch(mxb::Worker& worker,
                      int32_t nBatch,
                      bool single_batch);


    Result create_batch(std::function<bool(bsoncxx::document::value&& doc)> append, int32_t nBatch);

    std::vector<mxb::Json>           m_docs;
    std::vector<mxb::Json>::iterator m_it;
};


class NoSQLCursorBson : public NoSQLCursor
{
public:
    NoSQLCursorBson(const NoSQLCursorBson& rhs) = delete;

    static std::unique_ptr<NoSQLCursor> create(const std::string& ns,
                                               std::vector<bsoncxx::document::value>&& docs);


    void create_first_batch(mxb::Worker& worker,
                            bsoncxx::builder::basic::document& doc,
                            int32_t nBatch,
                            bool single_batch) override;
    void create_next_batch(mxb::Worker& worker,
                           bsoncxx::builder::basic::document& doc,
                           int32_t nBatch) override;

    void create_batch(mxb::Worker& worker,
                      int32_t nBatch,
                      bool single_batch,
                      size_t* pnSize_of_documents,
                      std::vector<bsoncxx::document::value>* pDocuments) override;

    int32_t nRemaining() const override;

private:
    NoSQLCursorBson(const std::string& ns, std::vector<bsoncxx::document::value>&& docs);

    enum class Result
    {
        PARTIAL,
        COMPLETE
    };

    void create_batch(mxb::Worker& worker,
                      bsoncxx::builder::basic::document& doc,
                      const std::string& which_batch,
                      int32_t nBatch,
                      bool single_batch);

    void create_batch(mxb::Worker& worker,
                      int32_t nBatch,
                      bool single_batch);


    Result create_batch(std::function<bool(bsoncxx::document::value& doc)> append, int32_t nBatch);

    std::vector<bsoncxx::document::value>           m_docs;
    std::vector<bsoncxx::document::value>::iterator m_it;
};

}
