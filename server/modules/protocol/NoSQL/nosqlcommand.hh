/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include "nosqlprotocol.hh"
#include <string>
#include <sstream>
#include <vector>
#include <maxscale/buffer.hh>
#include "nosqlcommon.hh"

namespace nosql
{

class Database;

namespace command
{

template<class ConcreteCommand>
struct IsAdmin
{
    static const bool is_admin {false};
};
}

//
// Command
//
class Command
{
public:
    static const int32_t MAX_PAYLOAD_LEN = 0xffffff;
    static const int32_t MAX_PACKET_LEN = MYSQL_HEADER_LEN + MAX_PAYLOAD_LEN;

    virtual ~Command();

    Database& database() const
    {
        return m_database;
    }

    const GWBUF& request() const
    {
        return m_request;
    }

    virtual bool is_admin() const;

    virtual bool is_silent() const
    {
        return m_response_kind == ResponseKind::NONE;
    }

    virtual bool is_get_last_error() const
    {
        return false;
    }

    virtual bool session_must_be_ready() const
    {
        return true;
    }

    virtual std::string description() const = 0;

    virtual std::string to_json() const;

    const std::string& last_statement() const
    {
        return m_last_statement;
    }

    virtual void authenticate()
    {
    }

    virtual void authorize(uint32_t role_mask)
    {
    }

    class Response final
    {
    public:
        Response(const Response&) = delete;
        Response& operator=(const Response&) = delete;

        Response(GWBUF* pData = nullptr, bool cacheable = false)
            : m_pData(pData)
            , m_cacheable(cacheable)
        {
        }

        Response(Response&& rhs)
            : m_pData(std::exchange(rhs.m_pData, nullptr))
            , m_cacheable(std::exchange(rhs.m_cacheable, false))
        {
        }

        Response& operator=(Response&& rhs)
        {
            if (this != &rhs)
            {
                m_pData = std::exchange(rhs.m_pData, nullptr);
                m_cacheable = std::exchange(rhs.m_cacheable, false);
            }

            return *this;
        }

        ~Response()
        {
            mxb_assert(!m_pData);
        }

        explicit operator bool() const
        {
            return m_pData != 0;
        }

        bool cacheable() const
        {
            return m_cacheable;
        }

        Command* command() const
        {
            return m_sCommand.get();
        }

        void set_command(std::unique_ptr<Command> sCommand)
        {
            mxb_assert(!m_sCommand);
            m_sCommand = std::move(sCommand);
        }

        void reset(GWBUF* pData, bool cacheable = false)
        {
            mxb_assert(!m_pData);

            m_pData = pData;
            m_cacheable = cacheable;
        }

        GWBUF* get() const
        {
            return m_pData;
        }

        GWBUF* release()
        {
            GWBUF* pData = std::exchange(m_pData, nullptr);
            m_cacheable = false;

            return pData;
        }

    private:
        GWBUF*                   m_pData     { nullptr };
        bool                     m_cacheable { false };
        std::unique_ptr<Command> m_sCommand;
    };

    virtual State execute(Response* pNoSQL_response) = 0;

    virtual State translate(GWBUF&& mariadb_response, Response* pNoSQL_response) = 0;

    enum class IsError
    {
        NO,
        YES
    };

    GWBUF* create_response(const bsoncxx::document::value& doc, IsError = IsError::NO) const;

    static GWBUF* create_reply_response(int32_t request_id,
                                        int32_t response_to,
                                        int64_t cursor_id,
                                        int32_t position,
                                        size_t size_of_documents,
                                        const std::vector<bsoncxx::document::value>& documents);

    GWBUF* create_reply_response(int64_t cursor_id,
                                 int32_t position,
                                 size_t size_of_documents,
                                 const std::vector<bsoncxx::document::value>& documents) const;

    enum class ResponseKind
    {
        NONE,
        REPLY,
        MSG,
        MSG_WITH_CHECKSUM
    };

    ResponseKind response_kind() const
    {
        return m_response_kind;
    }

protected:
    Command(Database* pDatabase,
            GWBUF* pRequest,
            int32_t request_id,
            ResponseKind response_kind)
        : m_database(*pDatabase)
        , m_request(pRequest->shallow_clone())
        , m_request_id(request_id)
        , m_response_kind(response_kind)
    {
    }

    void free_request();

    void send_downstream(const std::string& sql);

    void send_downstream_via_loop(const std::string& sql);

    void throw_unexpected_packet();

    mxs::RoutingWorker& worker() const;

    MXS_SESSION& session();

    Database&     m_database;
    GWBUF         m_request;
    const int32_t m_request_id;
    std::string   m_last_statement;

private:
    void log_back(const char* zContext, const bsoncxx::document::value& doc) const;

    static std::pair<GWBUF*, uint8_t*> create_reply_response_buffer(int32_t request_id,
                                                                    int32_t response_to,
                                                                    int64_t cursor_id,
                                                                    int32_t position,
                                                                    size_t size_of_documents,
                                                                    size_t nDocuments,
                                                                    IsError is_error);

    GWBUF* create_reply_response(const bsoncxx::document::value& doc, IsError is_error) const;

    GWBUF* create_msg_response(const bsoncxx::document::value& doc) const;

    ResponseKind m_response_kind;
};
}
