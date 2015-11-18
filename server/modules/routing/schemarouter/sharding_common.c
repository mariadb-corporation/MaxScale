/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2015
 */

#include <sharding_common.h>

/**
 * Extract the database name from a COM_INIT_DB or literal USE ... query.
 * @param buf Buffer with the database change query
 * @param str Pointer where the database name is copied
 * @return True for success, false for failure
 */
bool extract_database(GWBUF* buf, char* str)
{
     uint8_t* packet;
    char *saved,*tok,*query = NULL;
    bool succp = true;
    unsigned int plen;

    packet = GWBUF_DATA(buf);
    plen = gw_mysql_get_byte3(packet) - 1;

    /** Copy database name from MySQL packet to session */
    if(query_classifier_get_operation(buf) == QUERY_OP_CHANGE_DB)
    {
	query = modutil_get_SQL(buf);
	tok = strtok_r(query," ;",&saved);
	if(tok == NULL || strcasecmp(tok,"use") != 0)
	{
	    MXS_ERROR("extract_database: Malformed chage database packet.");
	    succp = false;
	    goto retblock;
	}

	tok = strtok_r(NULL," ;",&saved);
	if(tok == NULL)
	{
	    MXS_ERROR("extract_database: Malformed chage database packet.");
	    succp = false;
	    goto retblock;
	}

	strncpy(str,tok,MYSQL_DATABASE_MAXLEN);
    }
    else
    {
	memcpy(str,packet + 5,plen);
	memset(str + plen,0,1);
    }
    retblock:
    free(query);
    return succp;
}

/**
 * Create a fake error message from a DCB.
 * @param fail_str Custom error message
 * @param dcb DCB to use as the origin of the error
 */
void create_error_reply(char* fail_str,DCB* dcb)
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
 * @param buf	Buffer containing the database change query
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
    char db[MYSQL_DATABASE_MAXLEN+1];
    if(GWBUF_LENGTH(buf) <= MYSQL_DATABASE_MAXLEN - 5)
    {
	/** Copy database name from MySQL packet to session */
	if(!extract_database(buf,db))
	{
	    succp = false;
	    goto retblock;
	}
	MXS_INFO("change_current_db: INIT_DB with database '%s'", db);
	/**
	 * Update the session's active database only if it's in the hashtable.
	 * If it isn't found, send a custom error packet to the client.
	 */

	if((target = (char*)hashtable_fetch(dbhash,(char*)db)) == NULL)
	{
	    succp = false;
	    goto retblock;
	}
	else
	{
	    strcpy(dest,db);
	    MXS_INFO("change_current_db: database is on server: '%s'.",target);
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
