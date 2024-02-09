/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>
#include <maxbase/externcmd.hh>
#include <maxscale/filter.hh>
#include <maxbase/stopwatch.hh>

#include <vector>

#include "ldi.hh"
#include "ldiparser.hh"

class LDISession;

class UploadTracker
{
public:
    UploadTracker();
    void bytes_uploaded(size_t bytes);

private:
    size_t                 m_bytes {0};
    size_t                 m_chunk {0};
    mxb::Clock::time_point m_start;
};

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
    LDI::Config::Values       m_config;
    std::string               m_file;
    std::string               m_bucket;
    UploadTracker             m_tracker;

private:
    static size_t read_callback(void* buffer, size_t size, size_t nitems, void* userdata);

    size_t handle_read(void* buffer, size_t length);
    void   log_upload_speed();
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

class LDLIConversion : public std::enable_shared_from_this<LDLIConversion>
{
public:
    LDLIConversion(MXS_SESSION* session, std::unique_ptr<mxb::ExternalCmd> cmd);
    ~LDLIConversion();
    void enqueue(GWBUF&& data);
    void stop();

private:
    void drain_queue();

    MXS_SESSION*                      m_session;
    std::unique_ptr<mxb::ExternalCmd> m_cmd;
    std::vector<GWBUF>                m_queue;
    std::mutex                        m_lock;
    UploadTracker                     m_tracker;
};

class LDISession : public maxscale::FilterSession
{
public:
    static LDISession* create(MXS_SESSION* pSession, SERVICE* pService, LDI* pFilter);

    bool routeQuery(GWBUF&& buffer) override;

    bool clientReply(GWBUF&& buffer, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;

private:
    friend class S3Download;

    enum State
    {
        IDLE,               // Normal state
        PREPARE,            // Waiting for fake LDLI response
        LOAD,               // Fake LDLI being processed
    };

    LDISession(MXS_SESSION* pSession, SERVICE* pService, LDI* pFilter);
    bool route_data(GWBUF&& buffer);
    bool route_end(GWBUF&& buffer);
    bool send_ok(int64_t rows_affected);
    bool missing_required_params();

    static char* set_key(void* self, const char* key, const char* begin, const char* end);
    static char* set_secret(void* self, const char* key, const char* begin, const char* end);
    static char* set_region(void* self, const char* key, const char* begin, const char* end);
    static char* set_host(void* self, const char* key, const char* begin, const char* end);
    static char* set_port(void* self, const char* key, const char* begin, const char* end);
    static char* set_protocol_version(void* self, const char* key, const char* begin, const char* end);

    State               m_state {IDLE};
    std::string         m_file;
    std::string         m_bucket;
    LDI::Config::Values m_config;
    LDI&                m_filter;

    // This is a non-deleting reference to the same LDISession. We need this to know whether the filter
    // session is still alive. The S3Download has a reference on the session which guarantees that the
    // MXS_SESSION remains alive but this does not necessarily guarantee that the filter session remains
    // alive: the filter sessions get deleted in ClientDCB::shutdown() after the it has been taken out of the
    // zombie queue. This means that each access to `this` must be done on the worker that owns the session
    // and the std::weak_ptr derived from this must be locked before use.
    std::shared_ptr<LDISession> m_self;
};
