#pragma once
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2020-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.hh"
#include "rwsplit_ps.hh"
#include <string>

#include <maxscale/modutil.h>


/** Enum for tracking client reply state */
enum reply_state_t
{
    REPLY_STATE_START,          /**< Query sent to backend */
    REPLY_STATE_DONE,           /**< Complete reply received */
    REPLY_STATE_RSET_COLDEF,    /**< Resultset response, waiting for column definitions */
    REPLY_STATE_RSET_ROWS       /**< Resultset response, waiting for rows */
};

typedef enum
{
    EXPECTING_NOTHING = 0,
    EXPECTING_WAIT_GTID_RESULT,
    EXPECTING_REAL_RESULT
} wait_gtid_state_t;

typedef std::map<uint32_t, uint32_t> BackendHandleMap; /** Internal ID to external ID */
typedef std::map<uint32_t, uint32_t> ClientHandleMap;  /** External ID to internal ID */

class RWBackend: public mxs::Backend
{
    RWBackend(const RWBackend&);
    RWBackend& operator=(const RWBackend&);

public:
    RWBackend(SERVER_REF* ref);
    ~RWBackend();

    inline reply_state_t get_reply_state() const
    {
        return m_reply_state;
    }

    inline void set_reply_state(reply_state_t state)
    {
        m_reply_state = state;
    }

    void add_ps_handle(uint32_t id, uint32_t handle);
    uint32_t get_ps_handle(uint32_t id) const;

    bool execute_session_command();
    bool write(GWBUF* buffer, response_type type = EXPECT_RESPONSE);
    void close(close_type type = CLOSE_NORMAL);

    inline void set_large_packet(bool value)
    {
        m_large_packet = value;
    }

    inline bool is_large_packet() const
    {
        return m_large_packet;
    }

    inline uint8_t current_command() const
    {
        return m_command;
    }

    bool reply_is_complete(GWBUF *buffer);

private:
    reply_state_t    m_reply_state;
    BackendHandleMap m_ps_handles; /**< Internal ID to backend PS handle mapping */
    bool             m_large_packet; /**< Used to store the state of the EOF packet
                                      *calculation for result sets when the result
                                      * contains very large rows */
    uint8_t          m_command;
};

typedef std::tr1::shared_ptr<RWBackend> SRWBackend;
typedef std::list<SRWBackend> SRWBackendList;

typedef std::tr1::unordered_set<std::string> TableSet;
typedef std::map<uint64_t, uint8_t>          ResponseMap;

/** List of slave responses that arrived before the master */
typedef std::list< std::pair<SRWBackend, uint8_t> > SlaveResponseList;

/** Map of COM_STMT_EXECUTE targets by internal ID */
typedef std::tr1::unordered_map<uint32_t, SRWBackend> ExecMap;

/**
 * The client session of a RWSplit instance
 */
class RWSplitSession
{
    RWSplitSession(const RWSplitSession&);
    RWSplitSession& operator=(const RWSplitSession&);

public:

    /**
     * Create a new router session
     *
     * @param instance Router instance
     * @param session  The session object
     *
     * @return New router session
     */
    static RWSplitSession* create(RWSplit* router, MXS_SESSION* session);

    // TODO: Make member variables private
    skygw_chk_t             rses_chk_top;
    bool                    rses_closed; /**< true when closeSession is called */
    SRWBackendList          backends; /**< List of backend servers */
    SRWBackend              current_master; /**< Current master server */
    SRWBackend              target_node; /**< The currently locked target node */
    SRWBackend              prev_target; /**< The previous target where a query was sent */
    bool                    large_query; /**< Set to true when processing payloads >= 2^24 bytes */
    Config                  rses_config; /**< copied config info from router instance */
    int                     rses_nbackends;
    enum ld_state           load_data_state; /**< Current load data state */
    bool                    have_tmp_tables;
    uint64_t                rses_load_data_sent; /**< How much data has been sent */
    DCB*                    client_dcb;
    uint64_t                sescmd_count;
    int                     expected_responses; /**< Number of expected responses to the current query */
    GWBUF*                  query_queue; /**< Queued commands waiting to be executed */
    RWSplit*                router; /**< The router instance */
    TableSet                temp_tables; /**< Set of temporary tables */
    mxs::SessionCommandList sescmd_list; /**< List of executed session commands */
    ResponseMap             sescmd_responses; /**< Response to each session command */
    SlaveResponseList       slave_responses; /**< Slaves that replied before the master */
    uint64_t                sent_sescmd; /**< ID of the last sent session command*/
    uint64_t                recv_sescmd; /**< ID of the most recently completed session command */
    PSManager               ps_manager;  /**< Prepared statement manager*/
    ClientHandleMap         ps_handles;  /**< Client PS handle to internal ID mapping */
    ExecMap                 exec_map; /**< Map of COM_STMT_EXECUTE statement IDs to Backends */
    std::string             gtid_pos; /**< Gtid position for causal read */
    wait_gtid_state_t       wait_gtid_state; /**< Determine boundray of wait gtid result and client query result */
    uint32_t                next_seq; /**< Next packet'ssequence number */
    skygw_chk_t             rses_chk_tail;

private:
    RWSplitSession(RWSplit* instance, MXS_SESSION* session,
                   const SRWBackendList& backends, const SRWBackend& master);
};

/**
 * Helper function to convert reply_state_t to string
 */
static inline const char* rstostr(reply_state_t state)
{
    switch (state)
    {
    case REPLY_STATE_START:
        return "REPLY_STATE_START";

    case REPLY_STATE_DONE:
        return "REPLY_STATE_DONE";

    case REPLY_STATE_RSET_COLDEF:
        return "REPLY_STATE_RSET_COLDEF";

    case REPLY_STATE_RSET_ROWS:
        return "REPLY_STATE_RSET_ROWS";
    }

    ss_dassert(false);
    return "UNKNOWN";
}

/**
 * @brief Get the internal ID for the given binary prepared statement
 *
 * @param rses   Router client session
 * @param buffer Buffer containing a binary protocol statement other than COM_STMT_PREPARE
 *
 * @return The internal ID of the prepared statement that the buffer contents refer to
 */
uint32_t get_internal_ps_id(RWSplitSession* rses, GWBUF* buffer);
