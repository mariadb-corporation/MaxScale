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
#include <log_manager.h>

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

#include <item_func.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

extern int lm_enabled_logfiles_bitmask;

#define QTYPE_LESS_RESTRICTIVE_THAN_WRITE(t) (t<QUERY_TYPE_WRITE ? true : false)

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

        ss_info_dassert(query != NULL, ("query_str is NULL"));
        
        query_str = const_cast<char*>(query);
        LOGIF(LT, (skygw_log_write(
                LOGFILE_TRACE,
                "%lu [skygw_query_classifier_get_type] Query : \"%s\"",
                pthread_self(),
                query_str)));
        
        /** Get server handle */
        mysql = mysql_init(NULL);
        
        if (mysql == NULL) {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : call to mysql_real_connect failed due %d, %s.",
                        mysql_errno(mysql),
                        mysql_error(mysql))));
                
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
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to create thread context for parsing. "
                        "Exiting.")));
                goto return_thd;
        }
        mysql->thd = thd;
        init_embedded_mysql(mysql, client_flags);
        failp = check_embedded_connection(mysql, db);

        if (failp) {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Call to check_embedded_connection failed. "
                        "Exiting.")));
                goto return_err_with_thd;
        }
        thd->clear_data_list();

        /** Check that we are calling the client functions in right order */
        if (mysql->status != MYSQL_STATUS_READY) {
                set_mysql_error(mysql, CR_COMMANDS_OUT_OF_SYNC, unknown_sqlstate);
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Invalid status %d in embedded server. "
                        "Exiting.")));
                goto return_err_with_thd;
        }
        /** Clear result variables */
        thd->current_stmt= NULL;
        thd->store_globals();
        /** 
         * We have to call free_old_query before we start to fill mysql->fields 
         * for new query. In the case of embedded server we collect field data
         * during query execution (not during data retrieval as it is in remote
         * client). So we have to call free_old_query here
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
        return f;
}


static bool create_parse_tree(
        THD* thd)
{
        Parser_state parser_state;
        bool         failp = FALSE;
        const char*  virtual_db = "skygw_virtual";
        
        if (parser_state.init(thd, thd->query(), thd->query_length())) {
                failp = TRUE;
                goto return_here;
        }
        mysql_reset_thd_for_next_command(thd);
        
        /** Set some database to thd so that parsing won't fail because of
         * missing database. Then parse. */
        failp = thd->set_db(virtual_db, strlen(virtual_db));

        if (failp) {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to set database in thread context.")));
        }
        failp = parse_sql(thd, &parser_state, NULL);

        if (failp) {
                LOGIF(LD, (skygw_log_write(
                        LOGFILE_DEBUG,
                        "%lu [readwritesplit:create_parse_tree] failed to "
                        "create parse tree.",
                        pthread_self())));
        }
return_here:
        return failp;
}

/** 
 * @node Set new query type if new is more restrictive than old. 
 *
 * Parameters:
 * @param qtype - <usage>
 *          <description>
 *
 * @param new_type - <usage>
 *          <description>
 *
 * @return 
 *
 * 
 * @details The implementation relies on that enumerated values correspond
 * to the restrictiviness of the value. That is, smaller value means less
 * restrictive, for example, QUERY_TYPE_READ is smaller than QUERY_TYPE_WRITE.
 *
 */
static skygw_query_type_t set_query_type(
        skygw_query_type_t* qtype,
        skygw_query_type_t  new_type)
{
        *qtype = MAX(*qtype, new_type);
        return *qtype;
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
        Item*              item;
        /**
         * By default, if sql_log_bin, that is, recording data modifications
         * to binary log, is disabled, gateway treats operations normally.
         * Effectively nothing is replicated.
         * When force_data_modify_op_replication is TRUE, gateway distributes
         * all write operations to all nodes.
         */
        bool               force_data_modify_op_replication;
        
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
                if (lex->option_type == OPT_GLOBAL)
                {
                        qtype = QUERY_TYPE_GLOBAL_WRITE;
                }
                else
                {
                        qtype =  QUERY_TYPE_SESSION_WRITE;
                }
                goto return_here;
        }
        
        /** Try to catch session modifications here */
        switch (lex->sql_command) {
            case SQLCOM_SET_OPTION:
                if (lex->option_type == OPT_GLOBAL)
                {
                    qtype = QUERY_TYPE_GLOBAL_WRITE;
                    break;
                }
                /**<! fall through */
            case SQLCOM_CHANGE_DB:
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

        if (QTYPE_LESS_RESTRICTIVE_THAN_WRITE(qtype)) {
                /**
                 * These values won't change qtype more restrictive than write.
                 * UDFs and procedures could possibly cause session-wide write,
                 * but unless their content is replicated this is a limitation
                 * of this implementation.
                 * In other words : UDFs and procedures are not allowed to
                 * perform writes which are not replicated but nede to repeat
                 * in every node.
                 * It is not sure if such statements exist. vraa 25.10.13
                 */

                /**
                 * Search for system functions, UDFs and stored procedures.
                 */
                for (item=thd->free_list; item != NULL; item=item->next) {
                        Item::Type itype;
                
                        itype = item->type();
                        LOGIF(LD, (skygw_log_write(
                                LOGFILE_DEBUG,
                                "%lu [resolve_query_type] Item %s:%s",
                                pthread_self(),
                                item->name,
                                STRITEMTYPE(itype))));
                        
                        if (itype == Item::SUBSELECT_ITEM) {
                                continue;
                        } else if (itype == Item::FUNC_ITEM) {
                                skygw_query_type_t
                                        func_qtype = QUERY_TYPE_UNKNOWN;
                                /**
                                 * Item types:
                                 * FIELD_ITEM = 0, FUNC_ITEM,
                                 * SUM_FUNC_ITEM,  STRING_ITEM,    INT_ITEM,
                                 * REAL_ITEM,      NULL_ITEM,      VARBIN_ITEM,
                                 * COPY_STR_ITEM,  FIELD_AVG_ITEM,
                                 * DEFAULT_VALUE_ITEM,             PROC_ITEM,
                                 * COND_ITEM,      REF_ITEM,       FIELD_STD_ITEM,
                                 * FIELD_VARIANCE_ITEM,
                                 * INSERT_VALUE_ITEM,
                                 * SUBSELECT_ITEM, ROW_ITEM,       CACHE_ITEM,
                                 * TYPE_HOLDER,    PARAM_ITEM,
                                 * TRIGGER_FIELD_ITEM,             DECIMAL_ITEM,
                                 * XPATH_NODESET,  XPATH_NODESET_CMP,
                                 * VIEW_FIXER_ITEM,
                                 * EXPR_CACHE_ITEM == 27
                                 **/
                        
                                Item_func::Functype ftype;
                                ftype = ((Item_func*)item)->functype();
                                /**
                                 * Item_func types:
                                 * 
                                 * UNKNOWN_FUNC = 0,EQ_FUNC,      EQUAL_FUNC,
                                 * NE_FUNC,         LT_FUNC,      LE_FUNC,
                                 * GE_FUNC,         GT_FUNC,      FT_FUNC,
                                 * LIKE_FUNC == 10, ISNULL_FUNC,  ISNOTNULL_FUNC,
                                 * COND_AND_FUNC,   COND_OR_FUNC, XOR_FUNC,
                                 * BETWEEN,         IN_FUNC,
                                 * MULT_EQUAL_FUNC, INTERVAL_FUNC,
                                 * ISNOTNULLTEST_FUNC == 20,
                                 * SP_EQUALS_FUNC,  SP_DISJOINT_FUNC,
                                 * SP_INTERSECTS_FUNC,
                                 * SP_TOUCHES_FUNC, SP_CROSSES_FUNC,
                                 * SP_WITHIN_FUNC,  SP_CONTAINS_FUNC,
                                 * SP_OVERLAPS_FUNC,
                                 * SP_STARTPOINT,   SP_ENDPOINT == 30,
                                 * SP_EXTERIORRING, SP_POINTN,    SP_GEOMETRYN,
                                 * SP_INTERIORRINGN,NOT_FUNC,     NOT_ALL_FUNC,
                                 * NOW_FUNC,        TRIG_COND_FUNC,
                                 * SUSERVAR_FUNC,   GUSERVAR_FUNC == 40,
                                 * COLLATE_FUNC,    EXTRACT_FUNC,
                                 * CHAR_TYPECAST_FUNC,
                                 * FUNC_SP,         UDF_FUNC,     NEG_FUNC,
                                 * GSYSVAR_FUNC == 47
                                 **/
                                switch (ftype) {
                                case Item_func::FUNC_SP:
                                        /**
                                         * An unknown (for maxscale) function / sp
                                         * belongs to this category.
                                         */
                                        func_qtype = QUERY_TYPE_WRITE;
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "functype FUNC_SP, stored proc "
                                                "or unknown function.",
                                                "%s:%s",
                                                pthread_self())));
                                        break;
                                case Item_func::UDF_FUNC:
                                        func_qtype = QUERY_TYPE_WRITE;
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "functype UDF_FUNC, user-defined "
                                                "function.",
                                                pthread_self())));
                                        break;
                                case Item_func::NOW_FUNC:
                                case Item_func::GSYSVAR_FUNC:
                                        func_qtype = QUERY_TYPE_LOCAL_READ;
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "functype NOW_FUNC, could be "
                                                "executed in MaxScale.",
                                                pthread_self())));
                                        break;
                                case Item_func::UNKNOWN_FUNC:
                                        if (strncmp(item->name, "DATABASE()", 10) == 0)
                                        {
                                                /** 'USE <db' */
                                                func_qtype = QUERY_TYPE_SESSION_WRITE;
                                        }
                                        else
                                        {
                                                func_qtype = QUERY_TYPE_READ;
                                        }
                                        /**
                                         * Many built-in functions are of this
                                         * type, for example, rand(), soundex(),
                                         * repeat() .
                                         */
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "functype UNKNOWN_FUNC, "
                                                "typically some system function.",
                                                pthread_self())));
                                        break;
                                default:
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "Unknown functype %d. Something "
                                                "has gone wrong.",
                                                pthread_self(),
                                                ftype)));
                                        break;
                                } /**< switch */
                                /**< Set new query type */
                                qtype = set_query_type(&qtype, func_qtype);
                        }
                        /**
                         * Write is as restrictive as it gets due functions,
                         * so break.
                         */
                        if (qtype == QUERY_TYPE_WRITE) {
                                break;
                        }
                } /**< for */
        } /**< if */
return_here:
        return qtype;
}
