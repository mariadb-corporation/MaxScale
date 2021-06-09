/*
 * Copyright (c) 2018 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
#pragma once

#include <maxscale/ccdefs.hh>

#include <maxscale/router.hh>
#include <maxscale/service.hh>

#include "rpl_event.hh"
#include "parser.hh"
#include "pinloki.hh"
#include "reader.hh"

namespace pinloki
{

class PinlokiSession : public mxs::RouterSession, public pinloki::parser::Handler
{
public:
    PinlokiSession(const PinlokiSession&) = delete;
    PinlokiSession& operator=(const PinlokiSession&) = delete;

    PinlokiSession(MXS_SESSION* pSession, Pinloki* router);
    virtual ~PinlokiSession();

    bool routeQuery(GWBUF* pPacket) override;
    bool clientReply(GWBUF* pPacket, const mxs::ReplyRoute& down, const mxs::Reply& reply) override;
    bool handleError(mxs::ErrorType type, GWBUF* pMessage,
                     mxs::Endpoint* pProblem, const mxs::Reply& pReply) override;

    // pinloki::parser::Handler API
    void select(const std::vector<std::string>& values) override;
    void set(const std::string& key, const std::string& value) override;
    void change_master_to(const parser::ChangeMasterValues& values) override;
    void start_slave() override;
    void stop_slave() override;
    void reset_slave() override;
    void show_slave_status(bool all) override;
    void show_master_status() override;
    void show_binlogs() override;
    void show_variables(const std::string& like) override;
    void master_gtid_wait(const std::string& gtid, int timeout) override;
    void purge_logs(const std::string& up_to) override;
    void error(const std::string& err) override;

private:
    uint8_t                 m_seq = 1;  // Packet sequence number, incremented for each sent packet
    Pinloki*                m_router;
    mxq::GtidList           m_gtid_list;
    std::unique_ptr<Reader> m_reader;
    int64_t                 m_heartbeat_period = 0;
    uint32_t                m_mgw_dcid = 0; // MASTER_GTID_WAIT delayed call

    mxs::Buffer package(const uint8_t* ptr, size_t size);

    bool send_event(const maxsql::RplEvent& event);
    void send(GWBUF* buffer);
};
}
