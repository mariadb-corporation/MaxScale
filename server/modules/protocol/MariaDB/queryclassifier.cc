/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <maxscale/protocol/mariadb/queryclassifier.hh>
#include <unordered_map>
#include <maxbase/alloc.h>
#include <maxsimd/multistmt.hh>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mariadb/mysql.hh>
#include <maxscale/protocol/mariadb/protocol_classes.hh>
#include <maxscale/protocol/mariadb/query_classifier.hh>

using mariadb::QueryClassifier;

namespace
{

const int QC_TRACE_MSG_LEN = 1000;

// Copy of mxs_mysql_extract_ps_id() in modules/protocol/MySQL/mysql_common.cc,
// but we do not want to create a dependency from maxscale-common to that.

uint32_t mysql_extract_ps_id(GWBUF* buffer)
{
    uint32_t rval = 0;
    uint8_t id[MYSQL_PS_ID_SIZE];

    if (gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET, sizeof(id), id) == sizeof(id))
    {
        rval = gw_mysql_get_byte4(id);
    }

    return rval;
}

std::string qc_mysql_get_current_db(MXS_SESSION* session)
{
    return session->client_connection()->current_db();
}

// Copied from mysql_common.c
bool qc_mysql_is_ps_command(uint8_t cmd)
{
    return cmd == MXS_COM_STMT_EXECUTE
           || cmd == MXS_COM_STMT_BULK_EXECUTE
           || cmd == MXS_COM_STMT_SEND_LONG_DATA
           || cmd == MXS_COM_STMT_CLOSE
           || cmd == MXS_COM_STMT_FETCH
           || cmd == MXS_COM_STMT_RESET;
}

// Copied from mysql_common.cc
uint32_t qc_mysql_extract_ps_id(GWBUF* buffer)
{
    uint32_t rval = 0;
    uint8_t id[MYSQL_PS_ID_SIZE];

    if (gwbuf_copy_data(buffer, MYSQL_PS_ID_OFFSET, sizeof(id), id) == sizeof(id))
    {
        rval = gw_mysql_get_byte4(id);
    }

    return rval;
}

uint16_t qc_extract_ps_param_count(GWBUF* buffer)
{
    uint16_t rval = 0;
    uint8_t params[MYSQL_PS_PARAMS_SIZE];

    if (gwbuf_copy_data(buffer, MYSQL_PS_PARAMS_OFFSET, sizeof(params), params) == sizeof(params))
    {
        rval = gw_mysql_get_byte2(params);
    }

    return rval;
}

bool is_packet_a_query(int packet_type)
{
    return packet_type == MXS_COM_QUERY;
}

bool check_for_sp_call(GWBUF* buf, uint8_t packet_type)
{
    return packet_type == MXS_COM_QUERY && qc_get_operation(buf) == QUERY_OP_CALL;
}

bool are_multi_statements_allowed(MXS_SESSION* pSession)
{
    auto ses = static_cast<MYSQL_session*>(pSession->protocol_data());
    return (ses->client_caps.basic_capabilities & GW_MYSQL_CAPABILITIES_MULTI_STATEMENTS) != 0;
}

uint32_t get_prepare_type(GWBUF* buffer)
{
    uint32_t type = QUERY_TYPE_UNKNOWN;

    if (mxs_mysql_get_command(buffer) == MXS_COM_STMT_PREPARE)
    {
#ifdef SS_DEBUG
        GWBUF* stmt = gwbuf_deep_clone(buffer);
        GWBUF_DATA(stmt)[4] = MXS_COM_QUERY;
        mxb_assert(qc_get_type_mask(stmt) == (qc_get_type_mask(buffer) & ~QUERY_TYPE_PREPARE_STMT));
        gwbuf_free(stmt);
#endif

        type = qc_get_type_mask(buffer) & ~QUERY_TYPE_PREPARE_STMT;
    }
    else
    {
        GWBUF* stmt = qc_get_preparable_stmt(buffer);

        if (stmt)
        {
            type = qc_get_type_mask(stmt);
        }
    }

    return type;
}

std::string get_text_ps_id(GWBUF* buffer)
{
    std::string rval;
    char* name = qc_get_prepare_name(buffer);

    if (name)
    {
        rval = name;
        MXS_FREE(name);
    }

    return rval;
}

bool relates_to_previous_stmt(GWBUF* pBuffer)
{
    const QC_FUNCTION_INFO* infos = nullptr;
    size_t n_infos = 0;
    qc_get_function_info(pBuffer, &infos, &n_infos);

    for (size_t i = 0; i < n_infos; ++i)
    {
        if (strcasecmp(infos[i].name, "FOUND_ROWS") == 0)
        {
            return true;
        }
    }

    return false;
}

bool foreach_table(QueryClassifier& qc,
                   MXS_SESSION* pSession,
                   GWBUF* querybuf,
                   bool (* func)(QueryClassifier& qc, const std::string&))
{
    bool rval = true;

    for (const auto& t : qc_get_table_names(querybuf, true))
    {
        std::string table;

        if (t.find('.') == std::string::npos)
        {
            table = qc_mysql_get_current_db(pSession) + '.' + t;
        }
        else
        {
            table = t;
        }

        if (!func(qc, table))
        {
            rval = false;
            break;
        }
    }

    return rval;
}
}

namespace mariadb
{

class QueryClassifier::PSManager
{
    PSManager(const PSManager&) = delete;
    PSManager& operator=(const PSManager&) = delete;

public:
    struct PreparedStmt
    {
        uint32_t type = 0;
        uint16_t param_count = 0;
        bool     route_to_last_used = false;
    };

    PSManager()
    {
    }

    ~PSManager()
    {
    }

    void store(GWBUF* buffer, uint32_t id)
    {
        mxb_assert(mxs_mysql_get_command(buffer) == MXS_COM_STMT_PREPARE
                   || qc_query_is_type(qc_get_type_mask(buffer),
                                       QUERY_TYPE_PREPARE_NAMED_STMT));

        PreparedStmt stmt;
        stmt.type = get_prepare_type(buffer);
        stmt.route_to_last_used = relates_to_previous_stmt(buffer);

        switch (mxs_mysql_get_command(buffer))
        {
        case MXS_COM_QUERY:
            m_text_ps.emplace(get_text_ps_id(buffer), std::move(stmt));
            break;

        case MXS_COM_STMT_PREPARE:
            m_binary_ps.emplace(id, std::move(stmt));
            break;

        default:
            mxb_assert(!true);
            break;
        }
    }

    const PreparedStmt* get(uint32_t id) const
    {
        const PreparedStmt* rval = nullptr;
        BinaryPSMap::const_iterator it = m_binary_ps.find(id);

        if (it != m_binary_ps.end())
        {
            rval = &it->second;
        }
        else if (id != MARIADB_PS_DIRECT_EXEC_ID)
        {
            MXS_WARNING("Using unknown prepared statement with ID %u", id);
        }

        return rval;
    }

    const PreparedStmt* get(std::string id) const
    {
        const PreparedStmt* rval = nullptr;
        TextPSMap::const_iterator it = m_text_ps.find(id);

        if (it != m_text_ps.end())
        {
            rval = &it->second;
        }
        else
        {
            MXS_WARNING("Using unknown prepared statement with ID '%s'", id.c_str());
        }

        return rval;
    }

    void erase(std::string id)
    {
        if (m_text_ps.erase(id) == 0)
        {
            MXS_WARNING("Closing unknown prepared statement with ID '%s'", id.c_str());
        }
    }

    void erase(uint32_t id)
    {
        if (m_binary_ps.erase(id) == 0)
        {
            MXS_WARNING("Closing unknown prepared statement with ID %u", id);
        }
    }

    void erase(GWBUF* buffer)
    {
        uint8_t cmd = mxs_mysql_get_command(buffer);

        if (cmd == MXS_COM_QUERY)
        {
            erase(get_text_ps_id(buffer));
        }
        else if (qc_mysql_is_ps_command(cmd))
        {
            erase(mysql_extract_ps_id(buffer));
        }
        else
        {
            mxb_assert_message(!true, "QueryClassifier::PSManager::erase called with invalid query");
        }
    }

    void set_param_count(uint32_t id, uint16_t param_count)
    {
        m_binary_ps[id].param_count = param_count;
    }

    uint16_t param_count(uint32_t id) const
    {
        uint16_t rval = 0;
        auto it = m_binary_ps.find(id);

        if (it != m_binary_ps.end())
        {
            rval = it->second.param_count;
        }

        return rval;
    }

private:

    using BinaryPSMap = std::unordered_map<uint32_t, PreparedStmt>;
    using TextPSMap = std::unordered_map<std::string, PreparedStmt>;

private:
    BinaryPSMap m_binary_ps;
    TextPSMap   m_text_ps;
};

//
// QueryClassifier
//

QueryClassifier::QueryClassifier(Handler* pHandler,
                                 MXS_SESSION* pSession,
                                 mxs_target_t use_sql_variables_in)
    : m_pHandler(pHandler)
    , m_pSession(pSession)
    , m_use_sql_variables_in(use_sql_variables_in)
    , m_multi_statements_allowed(are_multi_statements_allowed(pSession))
    , m_sPs_manager(new PSManager)
{
}

void QueryClassifier::ps_store(GWBUF* pBuffer, uint32_t id)
{
    m_prev_ps_id = id;
    return m_sPs_manager->store(pBuffer, id);
}

void QueryClassifier::ps_erase(GWBUF* buffer)
{
    if (qc_mysql_is_ps_command(mxs_mysql_get_command(buffer)))
    {
        // Erase the type of the statement stored with the internal ID
        m_sPs_manager->erase(ps_id_internal_get(buffer));
    }
    else
    {
        // Not a PS command, we don't need the ID mapping
        m_sPs_manager->erase(buffer);
    }
}

bool QueryClassifier::query_type_is_read_only(uint32_t qtype) const
{
    bool rval = false;

    if (!qc_query_is_type(qtype, QUERY_TYPE_MASTER_READ)
        && !qc_query_is_type(qtype, QUERY_TYPE_WRITE)
        && (qc_query_is_type(qtype, QUERY_TYPE_READ)
            || qc_query_is_type(qtype, QUERY_TYPE_SHOW_TABLES)
            || qc_query_is_type(qtype, QUERY_TYPE_SHOW_DATABASES)
            || qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ)
            || qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ)
            || qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ)))
    {
        if (qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ))
        {
            if (m_use_sql_variables_in == TYPE_ALL)
            {
                rval = true;
            }
        }
        else
        {
            rval = true;
        }
    }

    return rval;
}

void QueryClassifier::process_routing_hints(const GWBUF::HintVector& hints, uint32_t* target)
{
    using Type = Hint::Type;
    const char max_rlag[] = "max_slave_replication_lag";

    bool check_more = true;
    for (auto it = hints.begin(); check_more && it != hints.end(); it++)
    {
        const Hint& hint = *it;
        if (m_pHandler->supports_hint(hint.type))
        {
            switch (hint.type)
            {
            case Type::ROUTE_TO_MASTER:
                // This means override, so we bail out immediately.
                *target = TARGET_MASTER;
                MXS_DEBUG("Hint: route to master");
                check_more = false;
                break;

            case Type::ROUTE_TO_NAMED_SERVER:
                // The router is expected to look up the named server.
                *target |= TARGET_NAMED_SERVER;
                MXS_DEBUG("Hint: route to named server: %s", hint.data.c_str());
                break;

            case Type::ROUTE_TO_UPTODATE_SERVER:
                // TODO: Add generic target type, never to be seem by RWS.
                mxb_assert(false);
                break;

            case Type::ROUTE_TO_ALL:
                // TODO: Add generic target type, never to be seem by RWS.
                mxb_assert(false);
                break;

            case Type::ROUTE_TO_LAST_USED:
                MXS_DEBUG("Hint: route to last used");
                *target = TARGET_LAST_USED;
                break;

            case Type::PARAMETER:
                if (strncasecmp(hint.data.c_str(), max_rlag, sizeof(max_rlag) - 1) == 0)
                {
                    *target |= TARGET_RLAG_MAX;
                }
                else
                {
                    MXS_ERROR("Unknown hint parameter '%s' when '%s' was expected.",
                              hint.data.c_str(), max_rlag);
                }
                break;

            case Type::ROUTE_TO_SLAVE:
                *target = TARGET_SLAVE;
                MXS_DEBUG("Hint: route to slave.");
                break;

            case Type::NONE:
                mxb_assert(!true);
                break;
            }
        }
    }
}

uint32_t QueryClassifier::get_route_target(uint8_t command, uint32_t qtype)
{
    auto session_data = m_pSession->protocol_data();
    bool trx_active = session_data->is_trx_active();
    uint32_t target = TARGET_UNDEFINED;
    bool load_active = (m_route_info.load_data_state() != LOAD_DATA_INACTIVE);
    mxb_assert(!load_active);

    /**
     * Prepared statements preparations should go to all servers
     */
    if (qc_query_is_type(qtype, QUERY_TYPE_PREPARE_STMT)
        || qc_query_is_type(qtype, QUERY_TYPE_PREPARE_NAMED_STMT)
        || command == MXS_COM_STMT_CLOSE
        || command == MXS_COM_STMT_RESET)
    {
        target = TARGET_ALL;
    }
    /**
     * These queries should be routed to all servers
     */
    else if (!load_active
             && (qc_query_is_type(qtype, QUERY_TYPE_SESSION_WRITE)
                 ||     /** Configured to allow writing user variables to all nodes */
                 (m_use_sql_variables_in == TYPE_ALL
                  && qc_query_is_type(qtype, QUERY_TYPE_USERVAR_WRITE))
                 || qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_WRITE)
                 ||     /** enable or disable autocommit are always routed to all */
                 qc_query_is_type(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT)
                 || qc_query_is_type(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT)))
    {
        /**
         * This is problematic query because it would be routed to all
         * backends but since this is SELECT that is not possible:
         * 1. response set is not handled correctly in clientReply and
         * 2. multiple results can degrade performance.
         *
         * Prepared statements are an exception to this since they do not
         * actually do anything but only prepare the statement to be used.
         * They can be safely routed to all backends since the execution
         * is done later.
         *
         * With prepared statement caching the task of routing
         * the execution of the prepared statements to the right server would be
         * an easy one. Currently this is not supported.
         */
        if (qc_query_is_type(qtype, QUERY_TYPE_READ))
        {
            MXS_WARNING("The query can't be routed to all "
                        "backend servers because it includes SELECT and "
                        "SQL variable modifications which is not supported. "
                        "Set use_sql_variables_in=master or split the "
                        "query to two, where SQL variable modifications "
                        "are done in the first and the SELECT in the "
                        "second one.");

            target = TARGET_MASTER;
        }
        target |= TARGET_ALL;
    }
    /**
     * Hints may affect on routing of the following queries
     */
    else if (!trx_active && !load_active && query_type_is_read_only(qtype))
    {
        target = TARGET_SLAVE;
    }
    else if (session_data->is_trx_read_only())
    {
        /* Force TARGET_SLAVE for READ ONLY transaction (active or ending) */
        target = TARGET_SLAVE;
    }
    else
    {
        mxb_assert(trx_active || load_active
                   || (qc_query_is_type(qtype, QUERY_TYPE_WRITE)
                       || qc_query_is_type(qtype, QUERY_TYPE_MASTER_READ)
                       || qc_query_is_type(qtype, QUERY_TYPE_SESSION_WRITE)
                       || (qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_WRITE)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || (qc_query_is_type(qtype, QUERY_TYPE_USERVAR_WRITE)
                           && m_use_sql_variables_in == TYPE_MASTER)
                       || qc_query_is_type(qtype, QUERY_TYPE_BEGIN_TRX)
                       || qc_query_is_type(qtype, QUERY_TYPE_ENABLE_AUTOCOMMIT)
                       || qc_query_is_type(qtype, QUERY_TYPE_DISABLE_AUTOCOMMIT)
                       || qc_query_is_type(qtype, QUERY_TYPE_ROLLBACK)
                       || qc_query_is_type(qtype, QUERY_TYPE_COMMIT)
                       || qc_query_is_type(qtype, QUERY_TYPE_EXEC_STMT)
                       || qc_query_is_type(qtype, QUERY_TYPE_CREATE_TMP_TABLE)
                       || qc_query_is_type(qtype, QUERY_TYPE_READ_TMP_TABLE)
                       || qc_query_is_type(qtype, QUERY_TYPE_UNKNOWN))
                   || qc_query_is_type(qtype, QUERY_TYPE_EXEC_STMT));

        target = TARGET_MASTER;
    }

    return target;
}

uint32_t QueryClassifier::ps_id_internal_get(GWBUF* pBuffer)
{
    // All COM_STMT type statements store the ID in the same place
    uint32_t id = mysql_extract_ps_id(pBuffer);

    // MARIADB_PS_DIRECT_EXEC_ID is a special ID that refers to the previous prepared statement
    if (id == MARIADB_PS_DIRECT_EXEC_ID && m_prev_ps_id)
    {
        return m_prev_ps_id;
    }

    return id;
}

void QueryClassifier::ps_store_response(uint32_t id, uint16_t param_count)
{
    // The previous PS ID can be larger than the ID of the response being stored if multiple prepared
    // statements were sent at the same time.
    mxb_assert(m_prev_ps_id == id);

    if (param_count)
    {
        m_sPs_manager->set_param_count(id, param_count);
    }
}

void QueryClassifier::log_transaction_status(GWBUF* querybuf, uint32_t qtype)
{
    if (m_route_info.large_query())
    {
        MXS_INFO("> Processing large request with more than 2^24 bytes of data");
    }
    else if (m_route_info.load_data_state() == QueryClassifier::LOAD_DATA_INACTIVE)
    {
        uint8_t* packet = GWBUF_DATA(querybuf);
        unsigned char command = packet[4];
        int len = 0;
        std::string sqldata;
        char* sql = (char*)"<non-SQL>";
        char* qtypestr = qc_typemask_to_string(qtype);

        if (qc_mysql_is_ps_command(command))
        {
            sqldata = "ID: " + std::to_string(mysql_extract_ps_id(querybuf));
            sql = (char*)sqldata.c_str();
            len = sqldata.length();
        }
        else
        {
            modutil_extract_SQL(querybuf, &sql, &len);
        }

        if (len > QC_TRACE_MSG_LEN)
        {
            len = QC_TRACE_MSG_LEN;
        }

        MXS_SESSION* ses = session();
        auto mariases = static_cast<MYSQL_session*>(ses->protocol_data());
        const char* autocommit = mariases->is_autocommit ? "[enabled]" : "[disabled]";
        const char* transaction = mariases->is_trx_active() ? "[open]" : "[not open]";
        uint32_t plen = MYSQL_GET_PACKET_LEN(querybuf);
        const char* querytype = qtypestr == NULL ? "N/A" : qtypestr;
        const char* hint = querybuf->hints.empty() ? "" : ", Hint:";
        const char* hint_type = querybuf->hints.empty() ? "" : Hint::type_to_str(querybuf->hints[0].type);

        MXS_INFO("> Autocommit: %s, trx is %s, cmd: (0x%02x) %s, plen: %u, type: %s, stmt: %.*s%s %s",
                 autocommit,
                 transaction,
                 command,
                 STRPACKETTYPE(command),
                 plen,
                 querytype,
                 len,
                 sql,
                 hint,
                 hint_type);

        MXS_FREE(qtypestr);
    }
    else if (m_route_info.load_data_state() == QueryClassifier::LOAD_DATA_END)
    {
        MXS_INFO("> LOAD DATA LOCAL INFILE finished: %lu bytes sent.", m_route_info.load_data_sent());
    }
    else
    {
        MXS_INFO("> Processing LOAD DATA LOCAL INFILE: %lu bytes sent.", m_route_info.load_data_sent());
    }
}

uint32_t QueryClassifier::determine_query_type(GWBUF* querybuf, int command)
{
    uint32_t type = QUERY_TYPE_UNKNOWN;

    switch (command)
    {
    case MXS_COM_QUIT:              /*< 1 QUIT will close all sessions */
    case MXS_COM_INIT_DB:           /*< 2 DDL must go to the master */
    case MXS_COM_REFRESH:           /*< 7 - I guess this is session but not sure */
    case MXS_COM_DEBUG:             /*< 0d all servers dump debug info to stdout */
    case MXS_COM_PING:              /*< 0e all servers are pinged */
    case MXS_COM_CHANGE_USER:       /*< 11 all servers change it accordingly */
    case MXS_COM_SET_OPTION:        /*< 1b send options to all servers */
    case MXS_COM_RESET_CONNECTION:  /*< 1f resets the state of all connections */
        type = QUERY_TYPE_SESSION_WRITE;
        break;

    case MXS_COM_CREATE_DB:             /**< 5 DDL must go to the master */
    case MXS_COM_DROP_DB:               /**< 6 DDL must go to the master */
    case MXS_COM_STMT_CLOSE:            /*< free prepared statement */
    case MXS_COM_STMT_SEND_LONG_DATA:   /*< send data to column */
    case MXS_COM_STMT_RESET:            /*< resets the data of a prepared statement */
        type = QUERY_TYPE_WRITE;
        break;

    case MXS_COM_FIELD_LIST:    /**< This is essentially SHOW COLUMNS */
        type = QUERY_TYPE_READ;
        break;

    case MXS_COM_QUERY:
        type = qc_get_type_mask(querybuf);
        break;

    case MXS_COM_STMT_PREPARE:
        type = qc_get_type_mask(querybuf);
        type |= QUERY_TYPE_PREPARE_STMT;
        break;

    case MXS_COM_STMT_EXECUTE:
        /** Parsing is not needed for this type of packet */
        type = QUERY_TYPE_EXEC_STMT;
        break;

    case MXS_COM_SHUTDOWN:      /**< 8 where should shutdown be routed ? */
    case MXS_COM_STATISTICS:    /**< 9 ? */
    case MXS_COM_PROCESS_INFO:  /**< 0a ? */
    case MXS_COM_CONNECT:       /**< 0b ? */
    case MXS_COM_PROCESS_KILL:  /**< 0c ? */
    case MXS_COM_TIME:          /**< 0f should this be run in gateway ? */
    case MXS_COM_DELAYED_INSERT:/**< 10 ? */
    case MXS_COM_DAEMON:        /**< 1d ? */
    default:
        break;
    }

    return type;
}

void QueryClassifier::check_create_tmp_table(GWBUF* querybuf, uint32_t type)
{
    if (qc_query_is_type(type, QUERY_TYPE_CREATE_TMP_TABLE))
    {
        std::string table;

        for (const auto& t : qc_get_table_names(querybuf, true))
        {
            if (strchr(t.c_str(), '.') == NULL)
            {
                table = qc_mysql_get_current_db(session()) + "." + t;
            }
            else
            {
                table = t;
            }
            break;
        }

        MXS_INFO("Added temporary table %s", table.c_str());

        /** Add the table to the set of temporary tables */
        m_route_info.add_tmp_table(table);
    }
}

bool QueryClassifier::is_read_tmp_table(GWBUF* querybuf, uint32_t qtype)
{
    bool rval = false;

    if (qc_query_is_type(qtype, QUERY_TYPE_READ)
        || qc_query_is_type(qtype, QUERY_TYPE_LOCAL_READ)
        || qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ)
        || qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ)
        || qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
    {
        if (!foreach_table(*this, m_pSession, querybuf, &QueryClassifier::find_table))
        {
            rval = true;
        }
    }

    return rval;
}

void QueryClassifier::check_drop_tmp_table(GWBUF* querybuf)
{
    if (qc_is_drop_table_query(querybuf))
    {
        foreach_table(*this, m_pSession, querybuf, &QueryClassifier::delete_table);
    }
}

/**
 * @brief Handle multi statement queries and load statements
 *
 * One of the possible types of handling required when a request is routed
 *
 * @param qc                   The query classifier
 * @param current_target       The current target
 * @param querybuf             Buffer containing query to be routed
 * @param packet_type          Type of packet (database specific)
 * @param qtype                Query type
 *
 * @return QueryClassifier::CURRENT_TARGET_MASTER if the session should be fixed
 *         to the master, QueryClassifier::CURRENT_TARGET_UNDEFINED otherwise.
 */
QueryClassifier::current_target_t QueryClassifier::handle_multi_temp_and_load(
    QueryClassifier::current_target_t current_target,
    GWBUF* querybuf,
    uint8_t packet_type,
    uint32_t* qtype)
{
    QueryClassifier::current_target_t rv = QueryClassifier::CURRENT_TARGET_UNDEFINED;

    /** Check for multi-statement queries. If no master server is available
     * and a multi-statement is issued, an error is returned to the client
     * when the query is routed. */
    if (current_target != QueryClassifier::CURRENT_TARGET_MASTER)
    {
        bool is_multi = check_for_sp_call(querybuf, packet_type);
        if (!is_multi && multi_statements_allowed() && packet_type == MXS_COM_QUERY)
        {
            // This is wasteful, the sql is extracted multiple times
            // it should be in the Context, after first call.
            const auto& sql = querybuf->get_sql();
            is_multi = maxsimd::is_multi_stmt(sql, &m_markers);
        }

        if (is_multi)
        {
            rv = QueryClassifier::CURRENT_TARGET_MASTER;
        }
    }

    /**
     * Check if the query has anything to do with temporary tables.
     */
    if (m_route_info.have_tmp_tables() && is_packet_a_query(packet_type))
    {

        check_drop_tmp_table(querybuf);
        if (is_read_tmp_table(querybuf, *qtype))
        {
            *qtype |= QUERY_TYPE_MASTER_READ;
        }
    }

    check_create_tmp_table(querybuf, *qtype);

    return rv;
}

uint16_t QueryClassifier::get_param_count(uint32_t id)
{
    return m_sPs_manager->param_count(id);
}

bool QueryClassifier::query_continues_ps(uint8_t cmd, uint32_t stmt_id, GWBUF* buffer)
{
    bool rval = false;
    uint8_t prev_cmd = m_route_info.command();

    if (prev_cmd == MXS_COM_STMT_SEND_LONG_DATA
        && (cmd == MXS_COM_STMT_EXECUTE || cmd == MXS_COM_STMT_SEND_LONG_DATA))
    {
        // PS execution must be sent to the same server where the data was sent
        rval = true;
    }
    else if (cmd == MXS_COM_STMT_FETCH)
    {
        // COM_STMT_FETCH should always go to the same target as the COM_STMT_EXECUTE
        rval = true;
    }

    return rval;
}

inline bool is_large_query(GWBUF* buf)
{
    uint32_t buflen = gwbuf_length(buf);

    // The buffer should contain at most (2^24 - 1) + 4 bytes ...
    mxb_assert(buflen <= MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN);
    // ... and the payload should be buflen - 4 bytes
    mxb_assert(MYSQL_GET_PAYLOAD_LEN(GWBUF_DATA(buf)) == buflen - MYSQL_HEADER_LEN);

    return buflen == MYSQL_HEADER_LEN + GW_MYSQL_MAX_PACKET_LEN;
}

QueryClassifier::RouteInfo QueryClassifier::update_route_info(
    QueryClassifier::current_target_t current_target,
    GWBUF* pBuffer)
{
    uint32_t route_target = TARGET_MASTER;
    uint8_t command = 0xFF;
    uint32_t type_mask = QUERY_TYPE_UNKNOWN;
    uint32_t stmt_id = 0;
    uint32_t len = gwbuf_length(pBuffer);

    // Stash the current state in case we need to roll it back
    m_prev_route_info = m_route_info;

    m_route_info.set_large_query(is_large_query(pBuffer));

    if (m_route_info.large_query())
    {
        // Trailing part of a multi-packet query, ignore it
        return m_route_info;
    }

    // Reset for every classification
    m_route_info.set_ps_continuation(false);

    if (m_route_info.load_data_state() == QueryClassifier::LOAD_DATA_INACTIVE
        && session_is_load_active(m_pSession))
    {
        m_route_info.set_load_data_state(QueryClassifier::LOAD_DATA_ACTIVE);
    }
    else if (m_route_info.load_data_state() == QueryClassifier::LOAD_DATA_END)
    {
        m_route_info.set_load_data_state(QueryClassifier::LOAD_DATA_INACTIVE);
    }

    // TODO: It may be sufficient to simply check whether we are in a read-only
    // TODO: transaction.
    auto protocol_data = session()->protocol_data();
    bool in_read_only_trx =
        (current_target != QueryClassifier::CURRENT_TARGET_UNDEFINED) && protocol_data->is_trx_read_only();

    if (m_route_info.load_data_state() == QueryClassifier::LOAD_DATA_ACTIVE)
    {
        m_route_info.append_load_data_sent(pBuffer);

        if (len == MYSQL_HEADER_LEN)
        {
            /** Empty packet signals end of LOAD DATA LOCAL INFILE, send it to master*/
            m_route_info.set_load_data_state(QueryClassifier::LOAD_DATA_END);
        }
    }
    else if (len > MYSQL_HEADER_LEN)
    {
        command = mxs_mysql_get_command(pBuffer);

        if (qc_mysql_is_ps_command(command))
        {
            stmt_id = ps_id_internal_get(pBuffer);
        }

        /**
         * If the session is inside a read-only transaction, we trust that the
         * server acts properly even when non-read-only queries are executed.
         * For this reason, we can skip the parsing of the statement completely.
         */
        if (in_read_only_trx)
        {
            type_mask = QUERY_TYPE_READ;
        }
        else
        {
            type_mask = QueryClassifier::determine_query_type(pBuffer, command);

            current_target = handle_multi_temp_and_load(current_target,
                                                        pBuffer,
                                                        command,
                                                        &type_mask);

            if (current_target == QueryClassifier::CURRENT_TARGET_MASTER)
            {
                /* If we do not have a master node, assigning the forced node is not
                 * effective since we don't have a node to force queries to. In this
                 * situation, assigning QUERY_TYPE_WRITE for the query will trigger
                 * the error processing. */
                if (!m_pHandler->lock_to_master())
                {
                    type_mask |= QUERY_TYPE_WRITE;
                }
            }
        }

        /**
         * Find out where to route the query. Result may not be clear; it is
         * possible to have a hint for routing to a named server which can
         * be either slave or master.
         * If query would otherwise be routed to slave then the hint determines
         * actual target server if it exists.
         *
         * route_target is a bitfield and may include :
         * TARGET_ALL
         * - route to all connected backend servers
         * TARGET_SLAVE[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to hints, then to slave and if those
         *   failed, eventually to master
         * TARGET_MASTER[|TARGET_NAMED_SERVER|TARGET_RLAG_MAX]
         * - route primarily according to the hints and if they failed,
         *   eventually to master
         */

        bool route_to_last_used = false;

        if (m_pHandler->is_locked_to_master())
        {
            /** The session is locked to the master */
            route_target = TARGET_MASTER;
        }
        else
        {
            if (!in_read_only_trx
                && command == MXS_COM_QUERY
                && qc_get_operation(pBuffer) == QUERY_OP_EXECUTE)
            {
                if (const auto* ps = m_sPs_manager->get(get_text_ps_id(pBuffer)))
                {
                    type_mask = ps->type;
                    route_to_last_used = ps->route_to_last_used;
                }
            }
            else if (qc_mysql_is_ps_command(command))
            {
                if (const auto* ps = m_sPs_manager->get(stmt_id))
                {
                    type_mask = ps->type;
                    route_to_last_used = ps->route_to_last_used;
                    m_route_info.set_ps_continuation(query_continues_ps(command, stmt_id, pBuffer));
                }
            }
            else if (command == MXS_COM_QUERY && relates_to_previous_stmt(pBuffer))
            {
                route_to_last_used = true;
            }

            route_target = get_route_target(command, type_mask);

            if (route_target == TARGET_SLAVE && route_to_last_used)
            {
                route_target = TARGET_LAST_USED;
            }
        }

        process_routing_hints(pBuffer->hints, &route_target);

        if (protocol_data->is_trx_ending() || qc_query_is_type(type_mask, QUERY_TYPE_BEGIN_TRX))
        {
            // Transaction is ending or starting
            m_route_info.set_trx_still_read_only(true);
        }
        else if (protocol_data->is_trx_active() && !query_type_is_read_only(type_mask))
        {
            // Transaction is no longer read-only
            m_route_info.set_trx_still_read_only(false);
        }
    }

    if (m_verbose && mxb_log_should_log(LOG_INFO))
    {
        log_transaction_status(pBuffer, type_mask);
    }

    m_route_info.set_target(route_target);
    m_route_info.set_command(command);
    m_route_info.set_type_mask(type_mask);
    m_route_info.set_stmt_id(stmt_id);

    return m_route_info;
}

// static
bool QueryClassifier::find_table(QueryClassifier& qc, const std::string& table)
{
    if (qc.m_route_info.is_tmp_table(table))
    {
        MXS_INFO("Query targets a temporary table: %s", table.c_str());
        return false;
    }

    return true;
}

// static
bool QueryClassifier::delete_table(QueryClassifier& qc, const std::string& table)
{
    qc.m_route_info.remove_tmp_table(table);
    return true;
}
}
