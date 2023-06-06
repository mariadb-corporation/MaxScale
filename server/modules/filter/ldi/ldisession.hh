/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxscale/filter.hh>
#include <maxscale/externcmd.hh>

#include <vector>

#include "ldi.hh"

class LDISession;

class S3Download
{
public:
    S3Download(LDISession* ldi);
    virtual ~S3Download();
    void load_data();

    virtual bool process(const char* ptr, size_t len) = 0;
    virtual bool complete() = 0;

protected:
    std::shared_ptr<LDISession> filter_session()
    {
        return m_ldi.lock();
    }

    // The MXS_SESSION can be accessed without checking for it since the S3Download holds a reference to it.
    MXS_SESSION* session()
    {
        return m_session;
    }

    bool route_data(GWBUF&& buffer);
    bool route_end(GWBUF&& buffer);
    bool send_ok(int64_t rows);

private:
    MXS_SESSION*              m_session;
    std::weak_ptr<LDISession> m_ldi;

    LDI::Config::Values m_config;
    std::string         m_file;
    std::string         m_bucket;

private:
    static size_t read_callback(void* buffer, size_t size, size_t nitems, void* userdata);
};

// Converts the data stream into a LOAD DATA LOCAL INFILE
class MariaDBLoader final : public S3Download
{
public:
    using S3Download::S3Download;
    bool process(const char* ptr, size_t len) override;
    bool complete() override;

private:
    bool send_packet();
    bool going_too_fast();

    uint8_t m_sequence {2};
    GWBUF   m_payload{4};
};

// Pipes the data stream into some external command
class CmdLoader final : public S3Download
{
public:
    CmdLoader(LDISession* ldi, std::unique_ptr<ExternalCmd> cmd);
    bool process(const char* ptr, size_t len) override;
    bool complete() override;

private:
    std::unique_ptr<ExternalCmd> m_cmd;
    int64_t                      m_rows {0};
};

class LDISession : public maxscale::FilterSession
{
public:
    static LDISession* create(MXS_SESSION* pSession, SERVICE* pService, const LDI* pFilter);

    bool routeQuery(GWBUF&& buffer) override;

    bool clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    friend class S3Download;

    enum State
    {
        IDLE,
        PREPARE,
        LOAD,
    };

    LDISession(MXS_SESSION* pSession, SERVICE* pService, const LDI* pFilter);
    bool route_data(GWBUF&& buffer);
    bool route_end(GWBUF&& buffer);
    bool send_ok(int64_t rows_affected);

    SERVER*                      get_xpand_node() const;
    std::unique_ptr<ExternalCmd> create_import_cmd(SERVER* node, std::string_view ldi_body);

    static char* set_key(void* self, const char* key, const char* begin, const char* end);
    static char* set_secret(void* self, const char* key, const char* begin, const char* end);
    static char* set_region(void* self, const char* key, const char* begin, const char* end);
    static char* set_import_user(void* self, const char* key, const char* begin, const char* end);
    static char* set_import_password(void* self, const char* key, const char* begin, const char* end);

    State               m_state {IDLE};
    std::string         m_file;
    std::string         m_bucket;
    LDI::Config::Values m_config;

    // This is a non-deleting reference to the same LDISession. We need this to know whether the filter
    // session is still alive. The S3Download has a reference on the session which guarantees that the
    // MXS_SESSION remains alive but this does not necessarily guarantee that the filter session remains
    // alive: the filter sessions get deleted in ClientDCB::shutdown() after the it has been taken out of the
    // zombie queue. This means that each access to `this` must be done on the worker that owns the session
    // and the std::weak_ptr derived from this must be locked before use.
    std::shared_ptr<LDISession> m_self;
};
