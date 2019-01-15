/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "readwritesplit.hh"
#include "rwsplitsession.hh"

#include <strings.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#include <maxscale/router.hh>
#include <maxscale/query_classifier.hh>
#include <maxscale/dcb.hh>
#include <maxscale/modinfo.h>
#include <maxscale/modutil.hh>
#include <maxscale/protocol/mysql.hh>
#include <maxscale/alloc.h>

/**
 * Functions within the read-write split router that are specific to
 * MySQL. The aim is to either remove these into a separate module or to
 * move them into the MySQL protocol modules.
 */

/*
 * The following functions are called from elsewhere in the router and
 * are defined in rwsplit_internal.hh.  They are not intended to be called
 * from outside this router.
 */

/* This could be placed in the protocol, with a new API entry point
 * It is certainly MySQL specific. Packet types are DB specific, but can be
 * assumed to be enums, which can be handled as integers without knowing
 * which DB is involved until the packet type needs to be interpreted.
 *
 */

/*
 * This appears to be MySQL specific
 */
/*
 * This one is problematic because it is MySQL specific, but also router
 * specific.
 */
/*
 * This is mostly router code, but it contains MySQL specific operations that
 * maybe could be moved to the protocol module. The modutil functions are mostly
 * MySQL specific and could migrate to the MySQL protocol; likewise the
 * utility to convert packet type to a string. The aim is for most of this
 * code to remain as part of the router.
 */
/**
 * @brief Operations to be carried out if request is for all backend servers
 *
 * If the choice of sending to all backends is in conflict with other bit
 * settings in route_target, then error messages are written to the log.
 *
 * Otherwise, the function route_session_write is called to carry out the
 * actual routing.
 *
 * @param route_target  Bit map indicating where packet should be routed
 * @param inst          Router instance
 * @param rses          Router session
 * @param querybuf      Query buffer containing packet
 * @param packet_type   Integer (enum) indicating type of packet
 * @param qtype         Query type
 * @return bool indicating whether the session can continue
 */
bool RWSplitSession::handle_target_is_all(route_target_t route_target,
                                          GWBUF* querybuf,
                                          int packet_type,
                                          uint32_t qtype)
{
    bool result = false;
    bool is_large = is_large_query(querybuf);

    if (TARGET_IS_MASTER(route_target) || TARGET_IS_SLAVE(route_target))
    {
        /**
         * Conflicting routing targets. Return an error to the client.
         */

        char* query_str = modutil_get_query(querybuf);
        char* qtype_str = qc_typemask_to_string(qtype);

        MXS_ERROR("Can't route %s:%s:\"%s\". SELECT with session data "
                  "modification is not supported if configuration parameter "
                  "use_sql_variables_in=all .",
                  STRPACKETTYPE(packet_type),
                  qtype_str,
                  (query_str == NULL ? "(empty)" : query_str));

        GWBUF* errbuf = modutil_create_mysql_err_msg(1,
                                                     0,
                                                     1064,
                                                     "42000",
                                                     "Routing query to backend failed. "
                                                     "See the error log for further details.");

        if (errbuf)
        {
            m_client->func.write(m_client, errbuf);
            result = true;
        }

        MXS_FREE(query_str);
        MXS_FREE(qtype_str);
    }
    else if (m_qc.large_query())
    {
        // TODO: Append to the already stored session command instead of disabling history
        MXS_INFO("Large session write, have to disable session command history");
        m_config.disable_sescmd_history = true;

        continue_large_session_write(querybuf, qtype);
        result = true;
    }
    else if (route_session_write(gwbuf_clone(querybuf), packet_type, qtype))
    {
        result = true;
        mxb::atomic::add(&m_router->stats().n_all, 1, mxb::atomic::RELAXED);
    }

    m_qc.set_large_query(is_large);

    return result;
}

/**
 * @brief Send an error message to the client telling that the server is in read only mode
 *
 * @param dcb Client DCB
 *
 * @return True if sending the message was successful, false if an error occurred
 */
bool send_readonly_error(DCB* dcb)
{
    bool succp = false;
    const char* errmsg = "The MariaDB server is running with the --read-only"
                         " option so it cannot execute this statement";
    GWBUF* err = modutil_create_mysql_err_msg(1,
                                              0,
                                              ER_OPTION_PREVENTS_STATEMENT,
                                              "HY000",
                                              errmsg);

    if (err)
    {
        succp = dcb->func.write(dcb, err);
    }
    else
    {
        MXS_ERROR("Memory allocation failed when creating client error message.");
    }

    return succp;
}
