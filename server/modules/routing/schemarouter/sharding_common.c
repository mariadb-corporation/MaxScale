/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include "sharding_common.h"
#include <maxscale/alloc.h>
#include <maxscale/poll.h>

/**
 * Extract the database name from a COM_INIT_DB or literal USE ... query.
 * @param buf Buffer with the database change query
 * @param str Pointer where the database name is copied
 * @return True for success, false for failure
 */
bool extract_database(GWBUF* buf, char* str)
{
    uint8_t* packet;
    char *saved, *tok, *query = NULL;
    bool succp = true;
    unsigned int plen;

    packet = GWBUF_DATA(buf);
    plen = gw_mysql_get_byte3(packet) - 1;

    /** Copy database name from MySQL packet to session */
    if (qc_get_operation(buf) == QUERY_OP_CHANGE_DB)
    {
        const char *delim = "` \n\t;";

        query = modutil_get_SQL(buf);
        tok = strtok_r(query, delim, &saved);

        if (tok == NULL || strcasecmp(tok, "use") != 0)
        {
            MXS_ERROR("extract_database: Malformed chage database packet.");
            succp = false;
            goto retblock;
        }

        tok = strtok_r(NULL, delim, &saved);

        if (tok == NULL)
        {
            MXS_ERROR("extract_database: Malformed change database packet.");
            succp = false;
            goto retblock;
        }

        strncpy(str, tok, MYSQL_DATABASE_MAXLEN);
    }
    else
    {
        memcpy(str, packet + 5, plen);
        memset(str + plen, 0, 1);
    }
retblock:
    MXS_FREE(query);
    return succp;
}

/**
 * Create a fake error message from a DCB.
 * @param fail_str Custom error message
 * @param dcb DCB to use as the origin of the error
 */
void create_error_reply(char* fail_str, DCB* dcb)
{
    MXS_INFO("change_current_db: failed to change database: %s", fail_str);
    GWBUF* errbuf = modutil_create_mysql_err_msg(1, 0, 1049, "42000", fail_str);

    if (errbuf == NULL)
    {
        MXS_ERROR("Creating buffer for error message failed.");
        return;
    }
    /** Set flags that help router to identify session commands reply */
    gwbuf_set_type(errbuf, GWBUF_TYPE_MYSQL);
    gwbuf_set_type(errbuf, GWBUF_TYPE_SESCMD_RESPONSE);
    gwbuf_set_type(errbuf, GWBUF_TYPE_RESPONSE_END);

    poll_add_epollin_event_to_dcb(dcb,
                                  errbuf);
}

/**
 * Read new database name from MYSQL_COM_INIT_DB packet or a literal USE ... COM_QUERY
 * packet, check that it exists in the hashtable and copy its name to MYSQL_session.
 *
 * @param dest Destination where the database name will be written
 * @param dbhash Hashtable containing valid databases
 * @param buf   Buffer containing the database change query
 *
 * @return true if new database is set, false if non-existent database was tried
 * to be set
 */
bool change_current_db(char* dest,
                       HASHTABLE* dbhash,
                       GWBUF* buf)
{
    char* target;
    bool succp;
    char db[MYSQL_DATABASE_MAXLEN + 1];
    if (GWBUF_LENGTH(buf) <= MYSQL_DATABASE_MAXLEN - 5)
    {
        /** Copy database name from MySQL packet to session */
        if (!extract_database(buf, db))
        {
            succp = false;
            goto retblock;
        }
        MXS_INFO("change_current_db: INIT_DB with database '%s'", db);
        /**
         * Update the session's active database only if it's in the hashtable.
         * If it isn't found, send a custom error packet to the client.
         */

        if ((target = (char*)hashtable_fetch(dbhash, (char*)db)) == NULL)
        {
            succp = false;
            goto retblock;
        }
        else
        {
            strcpy(dest, db);
            MXS_INFO("change_current_db: database is on server: '%s'.", target);
            succp = true;
            goto retblock;
        }
    }
    else
    {
        /** Create error message */
        MXS_ERROR("change_current_db: failed to change database: Query buffer too large");
        MXS_INFO("change_current_db: failed to change database: "
                 "Query buffer too large [%ld bytes]", GWBUF_LENGTH(buf));
        succp = false;
        goto retblock;
    }

retblock:
    return succp;
}
