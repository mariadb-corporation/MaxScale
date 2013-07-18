/**
 * @section LICENCE
 * 
 * This file is distributed as part of the SkySQL Gateway. It is
 * free software: you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 2.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA.
 * 
 * Copyright SkySQL Ab
 * 
 * @file 
 * 
 */

#define EMBEDDED_LIBRARY
#define MYSQL_YACC
#define MYSQL_LEX012
#define MYSQL_SERVER
#if defined(MYSQL_CLIENT)
# undef MYSQL_CLIENT
#endif

#include <query_classifier.h>
#include "../utils/skygw_types.h"
#include "../utils/skygw_debug.h"

#include <mysql.h>
#include <my_sys.h>
#include <my_global.h>
#include <my_dbug.h>
#include <my_base.h>
#include <sql_list.h>
#include <mysqld_error.h>
#include <sql_class.h>
#include <sql_lex.h>
#include <embedded_priv.h>
#include <sql_class.h>
#include <sql_lex.h>
#include <sql_parse.h>
#include <errmsg.h>
#include <client_settings.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>


static THD* get_or_create_thd_for_parsing(
        MYSQL* mysql,
        char*  query_str);

static unsigned long set_client_flags(
        MYSQL* mysql);

static bool create_parse_tree(
        THD* thd);

static skygw_query_type_t resolve_query_type(
        THD* thd);

/** 
 * @node (write brief function description here) 
 *
 * Parameters:
 * @param query_str - <usage>
 *          <description>
 *
 * @param client_flag - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
skygw_query_type_t skygw_query_classifier_get_type(
        const char*   query,
        unsigned long client_flags)
{
        MYSQL*             mysql;
        char*              query_str;
        const char*        user = "skygw";
        const char*        db = "skygw";
        THD*               thd;
        skygw_query_type_t qtype = QUERY_TYPE_UNKNOWN;
        bool               failp = FALSE;

        //ss_dfprintf(stderr, ">> skygw_query_classifier_get_type\n");
        ss_info_dassert(query != NULL, ("query_str is NULL"));
        
        query_str = const_cast<char*>(query);
        
        //fprintf(stderr, "   Query \"%s\"\n", query_str);
        
        /** Get server handle */
        mysql = mysql_init(NULL);
        
        if (mysql == NULL) {
            fprintf(stderr,
                    "mysql_real_connect failed, %d : %s\n",
                    mysql_errno(mysql),
                    mysql_error(mysql));
            mysql_library_end();
            goto return_without_server;
        }

        /** Set methods and authentication to mysql */
        mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, "libmysqld_skygw");
        mysql_options(mysql, MYSQL_OPT_USE_EMBEDDED_CONNECTION, NULL);
        mysql->methods = &embedded_methods;
        mysql->user    = my_strdup(user, MYF(0));
        mysql->db      = my_strdup(db, MYF(0));
        mysql->passwd  = NULL;
        
        /** Get one or create new THD object to be use in parsing */
        thd = get_or_create_thd_for_parsing(mysql, query_str);

        if (thd == NULL) {
            goto return_with_server_handle;
        }
        /** Create parse_tree inside thd */
        failp = create_parse_tree(thd);

        if (failp) {
            goto return_with_thd;
        }
        qtype = resolve_query_type(thd);
        
return_with_thd:
        (*mysql->methods->free_embedded_thd)(mysql);
        mysql->thd = 0;
return_with_server_handle:
        mysql_close(mysql);
        mysql_thread_end();
return_without_server:
        //ss_dfprintf(stderr,
        //            "<< skygw_query_classifier_get_type : %s\n",
        //            STRQTYPE(qtype));
        //ss_dfflush(stderr);
        return qtype;
}



/** 
 * @node (write brief function description here) 
 *
 * Parameters:
 * @param mysql - <usage>
 *          <description>
 *
 * @param query_str - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
static THD* get_or_create_thd_for_parsing(
        MYSQL* mysql,
        char*  query_str)
{
        THD*          thd    = NULL;
        unsigned long client_flags;
        char*         db     = mysql->options.db;
        bool          failp  = FALSE;
        size_t        query_len;

        //ss_dfprintf(stderr, "> get_or_create_thd_for_parsing\n");
        ss_info_dassert(mysql != NULL, ("mysql is NULL"));
        ss_info_dassert(query_str != NULL, ("query_str is NULL"));

        query_len = strlen(query_str);
        client_flags = set_client_flags(mysql);
        
        /** Get THD.
         * NOTE: Instead of creating new every time, THD instance could
         * be get from a pool of them.
         */
        thd = (THD *)create_embedded_thd(client_flags);

        if (thd == NULL) {
            ss_dfprintf(stderr, "Couldn't create embedded thd\n");
            goto return_thd;
        }
        mysql->thd = thd;
        init_embedded_mysql(mysql, client_flags);
        failp = check_embedded_connection(mysql, db);

        if (failp) {
            ss_dfprintf(stderr, "Checking embedded connection failed.\n");
            goto return_err_with_thd;
        }
        thd->clear_data_list();

        /** Check that we are calling the client functions in right order */
        if (mysql->status != MYSQL_STATUS_READY) {
            set_mysql_error(mysql, CR_COMMANDS_OUT_OF_SYNC, unknown_sqlstate);
            goto return_err_with_thd;
        }
        /* Clear result variables */
        thd->current_stmt= NULL;
        thd->store_globals();
        /* 
           We have to call free_old_query before we start to fill mysql->fields 
           for new query. In the case of embedded server we collect field data
           during query execution (not during data retrieval as it is in remote
           client). So we have to call free_old_query here
        */
        free_old_query(mysql);
        thd->extra_length = query_len;
        thd->extra_data = query_str;
        alloc_query(thd, query_str, query_len);
        goto return_thd;
        
return_err_with_thd:
        (*mysql->methods->free_embedded_thd)(mysql);
        thd = 0;
        mysql->thd = 0;
return_thd:
        //ss_dfprintf(stderr, "< get_or_create_thd_for_parsing : %p\n", thd);
        //ss_dfflush(stderr);
        return thd;
}



/** 
 * @node  Set client flags. This is copied from libmysqld.c:mysql_real_connect 
 *
 * Parameters:
 * @param mysql - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details (write detailed description here)
 *
 */
static unsigned long set_client_flags(
        MYSQL* mysql)
{
        unsigned long f = 0;
        //ss_dfprintf(stderr, "> set_client_flags\n");
        f |= mysql->options.client_flag;
        
        /* Send client information for access check */
        f |= CLIENT_CAPABILITIES;
        
        if (f & CLIENT_MULTI_STATEMENTS) {
            f |= CLIENT_MULTI_RESULTS;
        }
        /**
         * No compression in embedded as we don't send any data,
         * and no pluggable auth, as we cannot do a client-server dialog
         */
        f &= ~(CLIENT_COMPRESS | CLIENT_PLUGIN_AUTH);
        
        if (mysql->options.db != NULL) {
            f |= CLIENT_CONNECT_WITH_DB;
        }
        //ss_dfprintf(stderr, "< set_client_flags : %lu\n", f);
        //ss_dfflush(stderr);
        return f;
}


static bool create_parse_tree(
        THD* thd)
{
        Parser_state parser_state;
        bool         failp = FALSE;
        const char*  virtual_db = "skygw_virtual";
        //ss_dfprintf(stderr, "> create_parse_tree\n");
        
        if (parser_state.init(thd, thd->query(), thd->query_length())) {
            failp = TRUE;
            goto return_here;
        }
        mysql_reset_thd_for_next_command(thd, opt_userstat_running);
        
        /** Set some database to thd so that parsing won't fail because of
         * missing database. Then parse. */
        failp = thd->set_db(virtual_db, sizeof(virtual_db));

        if (failp) {
            fprintf(stderr, "Setting database for thd failed\n");
        }
        failp = parse_sql(thd, &parser_state, NULL);

        if (failp) {
            fprintf(stderr, "parse_sql failed\n");
        }
return_here:
        //ss_dfprintf(stderr, "< create_parse_tree : %s\n", STRBOOL(failp));
        //fflush(stderr);
        return failp;
}


/** 
 * @node Detect query type, read-only, write, or session update 
 *
 * Parameters:
 * @param thd - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details Query type is deduced by checking for certain properties
 * of them. The order is essential. Some SQL commands have multiple
 * flags set and changing the order in which flags are tested,
 * the resulting type may be different.
 *
 */
static skygw_query_type_t resolve_query_type(
        THD* thd)
{
        skygw_query_type_t qtype = QUERY_TYPE_UNKNOWN;
        LEX*               lex;
        /**
         * By default, if sql_log_bin, that is, recording data modifications
         * to binary log, is disabled, gateway treats operations normally.
         * Effectively nothing is replicated.
         * When force_data_modify_op_replication is TRUE, gateway distributes
         * all write operations to all nodes.
         */
        bool               force_data_modify_op_replication;
        
        //ss_dfprintf(stderr, "> resolve_query_type\n");
        ss_info_dassert(thd != NULL, ("thd is NULL\n"));

        force_data_modify_op_replication = FALSE;
        lex = thd->lex;
        
        /** SELECT ..INTO variable|OUTFILE|DUMPFILE */
        if (lex->result != NULL) {
            qtype = QUERY_TYPE_SESSION_WRITE;
            goto return_here;
        }
        /**
         * 1:ALTER TABLE, TRUNCATE, REPAIR, OPTIMIZE, ANALYZE, CHECK.
         * 2:CREATE|ALTER|DROP|TRUNCATE|RENAME TABLE, LOAD, CREATE|DROP|ALTER DB,
         *   CREATE|DROP INDEX, CREATE|DROP VIEW, CREATE|DROP TRIGGER,
         *   CREATE|ALTER|DROP EVENT, UPDATE, INSERT, INSERT(SELECT),
         *   DELETE, REPLACE, REPLACE(SELECT), CREATE|RENAME|DROP USER,
         *   GRANT, REVOKE, OPTIMIZE, CREATE|ALTER|DROP FUNCTION|PROCEDURE,
         *   CREATE SPFUNCTION, INSTALL|UNINSTALL PLUGIN
         */
        if (is_log_table_write_query(lex->sql_command) ||
            is_update_query(lex->sql_command))
        {
            if (thd->variables.sql_log_bin == 0 &&
                force_data_modify_op_replication)
            {
                qtype = QUERY_TYPE_SESSION_WRITE;
            } else {
                qtype = QUERY_TYPE_WRITE;
            }
            
            goto return_here;
        }

        /**
         * REVOKE ALL, ASSIGN_TO_KEYCACHE,
         * PRELOAD_KEYS, FLUSH, RESET, CREATE|ALTER|DROP SERVER
         */
        if (sql_command_flags[lex->sql_command] & CF_AUTO_COMMIT_TRANS) {
            qtype =  QUERY_TYPE_SESSION_WRITE;
            goto return_here;
        }
        
        /** Try to catch session modifications here */
        switch (lex->sql_command) {
            case SQLCOM_CHANGE_DB:
            case SQLCOM_SET_OPTION:
                qtype = QUERY_TYPE_SESSION_WRITE;
                break;

            case SQLCOM_SELECT:
                qtype = QUERY_TYPE_READ;
                break;

            case SQLCOM_CALL:
                qtype = QUERY_TYPE_WRITE;
                break;
                
            default:
                break;
        }
return_here:
        //ss_dfprintf(stderr, "< resolve_query_type : %s\n", STRQTYPE(qtype));
        return qtype;
}
