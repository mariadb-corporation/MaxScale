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
#include <mysql_client_server_protocol.h>

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
#include <set_var.h>
#include <strfunc.h>
#include <item_func.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static bool skygw_stmt_causes_implicit_commit(
        LEX*  lex,
        int* autocommit_stmt);

static int is_autocommit_stmt(
        LEX* lex);

static void parsing_info_set_plain_str(void* ptr, 
        char* str);

/**
 * Calls parser for the query includede in the buffer. Creates and adds parsing 
 * information to buffer if it doesn't exist already. Resolves the query type. 
 * 
 * @param querybuf buffer including the query and possibly the parsing information
 * 
 * @return query type
 */
skygw_query_type_t query_classifier_get_type(
        GWBUF* querybuf)
{
        MYSQL*             mysql;
        skygw_query_type_t qtype = QUERY_TYPE_UNKNOWN;
        bool               succp;
        
        ss_info_dassert(querybuf != NULL, ("querybuf is NULL"));
        
        /** Create parsing info for the query and store it to buffer */
        succp = query_is_parsed(querybuf);
        
        if (!succp)
        {
                succp = parse_query(querybuf);
        }
        /** Read thd pointer and resolve the query type with it. */
        if (succp)
        {
                parsing_info_t* pi;
                
                pi = (parsing_info_t*)gwbuf_get_buffer_object_data(querybuf, 
                                                                   GWBUF_PARSING_INFO);
                
                if (pi != NULL)
                {
                        mysql = (MYSQL *)pi->pi_handle;

                        /** Find out the query type */
                        if (mysql != NULL)
                        {
                                qtype = resolve_query_type((THD *)mysql->thd);
                        }
                }
        }
        return qtype;
}

/**
 * Create parsing info and try to parse the query included in the query buffer.
 * Store pointer to created parse_tree_t object to buffer.
 * 
 * @param querybuf buffer including the query and possibly the parsing information
 * 
 * @return true if succeed, false otherwise
 */
bool parse_query (
        GWBUF* querybuf)
{
        bool            succp;
        THD*            thd;
        uint8_t*        data;
        size_t          len;
        char*           query_str;
        parsing_info_t* pi;
        
        CHK_GWBUF(querybuf);
        /** Do not parse without releasing previous parse info first */
        ss_dassert(!query_is_parsed(querybuf));
         
        if (query_is_parsed(querybuf))
        {
                return false;
        }
        /** Create parsing info */
        pi = parsing_info_init(parsing_info_done);
        
        if (pi == NULL)
        {
                succp = false;
                goto retblock;
        }        
        /** Extract query and copy it to different buffer */
        data = (uint8_t*)GWBUF_DATA(querybuf);
        len = MYSQL_GET_PACKET_LEN(data)-1; /*< distract 1 for packet type byte */        
        query_str = (char *)malloc(len+1);
        
        if (query_str == NULL)
        {
                /** Free parsing info data */
                parsing_info_done(pi);
                succp = false;
                goto retblock;
        }
        memcpy(query_str, &data[5], len);
        memset(&query_str[len], 0, 1);
        parsing_info_set_plain_str(pi, query_str);
        
        /** Get one or create new THD object to be use in parsing */
        thd = get_or_create_thd_for_parsing((MYSQL *)pi->pi_handle, query_str);
        
        if (thd == NULL)
        {
                /** Free parsing info data */
                parsing_info_done(pi);
                succp = false;
                goto retblock;
        }
        /** 
         * Create parse_tree inside thd.
         * thd and lex are readable even if creating parse tree fails.
         */
        create_parse_tree(thd);
        /** Add complete parsing info struct to the query buffer */
        gwbuf_add_buffer_object(querybuf, 
                                GWBUF_PARSING_INFO, 
                                (void *)pi, 
                                parsing_info_done);
        
        succp = true;
retblock:
        return succp;
}


/**
 * If buffer has non-NULL gwbuf_parsing_info it is parsed and it has parsing
 * information included.
 * 
 * @param buf buffer being examined
 * 
 * @return true or false
 */
bool query_is_parsed(
        GWBUF* buf)
{
        CHK_GWBUF(buf);
        return GWBUF_IS_PARSED(buf);
}


/**
 * Create a thread context, thd, init embedded server, connect to it, and allocate
 * query to thd.
 *
 * Parameters:
 * @param mysql         Database handle
 * 
 * @param query_str     Query in plain txt string
 *
 * @return Thread context pointer
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
#if defined(SS_DEBUG_EXTRA)
	LOGIF(LM, (skygw_log_write_flush(
		LOGFILE_MESSAGE,
		"[readwritesplit:create_parse_tree] 1.")));
#endif
        if (parser_state.init(thd, thd->query(), thd->query_length())) {
                failp = TRUE;
                goto return_here;
        }
#if defined(SS_DEBUG_EXTRA)
        LOGIF(LM, (skygw_log_write_flush(
		LOGFILE_MESSAGE,
		"[readwritesplit:create_parse_tree] 2.")));
#endif	
	mysql_reset_thd_for_next_command(thd);

#if defined(SS_DEBUG_EXTRA)	
	LOGIF(LM, (skygw_log_write_flush(
		LOGFILE_MESSAGE,
		"[readwritesplit:create_parse_tree] 3.")));
#endif
        /** 
	 * Set some database to thd so that parsing won't fail because of
         * missing database. Then parse. 
	 */
        failp = thd->set_db(virtual_db, strlen(virtual_db));
#if defined(SS_DEBUG_EXTRA)	
	LOGIF(LM, (skygw_log_write_flush(
		LOGFILE_MESSAGE,
		"[readwritesplit:create_parse_tree] 4.")));
#endif
        if (failp) {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : Failed to set database in thread context.")));
        }
        failp = parse_sql(thd, &parser_state, NULL);

#if defined(SS_DEBUG_EXTRA)
	LOGIF(LM, (skygw_log_write_flush(
		LOGFILE_MESSAGE,
		"[readwritesplit:create_parse_tree] 5.")));
#endif	
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
static u_int32_t set_query_type(
        u_int32_t* qtype,
        u_int32_t  new_type)
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
        u_int32_t           type = QUERY_TYPE_UNKNOWN;
        int                 set_autocommit_stmt = -1; /*< -1 no, 0 disable, 1 enable */
        LEX*  lex;
        Item* item;
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
                type = QUERY_TYPE_GSYSVAR_WRITE;
                goto return_qtype;
        }
        
        if (skygw_stmt_causes_implicit_commit(
                lex, 
                &set_autocommit_stmt))
        {
                if (LOG_IS_ENABLED(LOGFILE_TRACE))
                {
                        if (sql_command_flags[lex->sql_command] & 
                                CF_IMPLICT_COMMIT_BEGIN)
                        {
                                skygw_log_write(
                                        LOGFILE_TRACE,
                                        "Implicit COMMIT before executing the "
                                        "next command.");
                        }
                        else if (sql_command_flags[lex->sql_command] & 
                                CF_IMPLICIT_COMMIT_END)
                        {
                                skygw_log_write(
                                        LOGFILE_TRACE,
                                        "Implicit COMMIT after executing the "
                                        "next command.");
                        }
                }
                
                if (set_autocommit_stmt == 1)
                {
                        type |= QUERY_TYPE_ENABLE_AUTOCOMMIT;
                }
                type |= QUERY_TYPE_COMMIT;
        } 
        
        if (set_autocommit_stmt == 0)
        {
                if (LOG_IS_ENABLED(LOGFILE_TRACE))
                {
                        skygw_log_write(
                                LOGFILE_TRACE,
                                "Disable autocommit : implicit START TRANSACTION"
                                " before executing the next command.");
                }
                type |= QUERY_TYPE_DISABLE_AUTOCOMMIT;  
                type |= QUERY_TYPE_BEGIN_TRX;
        }

        if (lex->option_type == OPT_GLOBAL)
        {
		/** 
		 * SHOW syntax http://dev.mysql.com/doc/refman/5.6/en/show.html
		 */
		if (lex->sql_command == SQLCOM_SHOW_VARIABLES)
		{
			type |= QUERY_TYPE_GSYSVAR_READ;
		}
		/**
		 * SET syntax http://dev.mysql.com/doc/refman/5.6/en/set-statement.html
		 */
		else if (lex->sql_command == SQLCOM_SET_OPTION)
		{
			type |= QUERY_TYPE_GSYSVAR_WRITE;
		}
		/**
		 * REVOKE ALL, ASSIGN_TO_KEYCACHE,
		 * PRELOAD_KEYS, FLUSH, RESET, CREATE|ALTER|DROP SERVER
		 */
		else 
		{
			type |= QUERY_TYPE_GSYSVAR_WRITE;
		}
		goto return_qtype;
        }
        else if (lex->option_type == OPT_SESSION)
        {
		/** 
		 * SHOW syntax http://dev.mysql.com/doc/refman/5.6/en/show.html
		 */
		if (lex->sql_command == SQLCOM_SHOW_VARIABLES)
		{
			type |= QUERY_TYPE_SYSVAR_READ;
		}
		/**
		 * SET syntax http://dev.mysql.com/doc/refman/5.6/en/set-statement.html
		 */
		else if (lex->sql_command == SQLCOM_SET_OPTION)
		{
			/** Either user- or system variable write */
			type |= QUERY_TYPE_GSYSVAR_WRITE;
		}
		goto return_qtype;
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
			/** Not replicated */
                        type |= QUERY_TYPE_SESSION_WRITE;
                } 
                else 
                {
			/** Written to binlog, that is, replicated except tmp tables */
                        type |= QUERY_TYPE_WRITE; /*< to master */
                        
                        if (lex->create_info.options & HA_LEX_CREATE_TMP_TABLE && 
			    lex->sql_command == SQLCOM_CREATE_TABLE)
                        {
				type |= QUERY_TYPE_CREATE_TMP_TABLE; /*< remember in router */
                        }		        
                }
                goto return_qtype;
        }
        
        /** Try to catch session modifications here */
        switch (lex->sql_command) {
		/** fallthrough */
                case SQLCOM_CHANGE_DB:
                case SQLCOM_DEALLOCATE_PREPARE:
                        type |= QUERY_TYPE_SESSION_WRITE;
                        break;

                case SQLCOM_SELECT:
                        type |= QUERY_TYPE_READ;
                        break;

                case SQLCOM_CALL:
                        type |= QUERY_TYPE_WRITE;
                        break;
                        
                case SQLCOM_BEGIN:
                        type |= QUERY_TYPE_BEGIN_TRX;
                        goto return_qtype;
                        break;
                
                case SQLCOM_COMMIT:
                        type |= QUERY_TYPE_COMMIT;
                        goto return_qtype;
                        break;
                        
                case SQLCOM_ROLLBACK:
                        type |= QUERY_TYPE_ROLLBACK;
                        goto return_qtype;
                        break;
                        
                case SQLCOM_PREPARE:
                        type |= QUERY_TYPE_PREPARE_NAMED_STMT;
                        goto return_qtype;
                        break;
                        
                default:
                        break;
        }
#if defined(UPDATE_VAR_SUPPORT)
        if (QTYPE_LESS_RESTRICTIVE_THAN_WRITE(type)) 
#endif
	if (QUERY_IS_TYPE(qtype, QUERY_TYPE_UNKNOWN) ||
		QUERY_IS_TYPE(qtype, QUERY_TYPE_LOCAL_READ) ||
		QUERY_IS_TYPE(qtype, QUERY_TYPE_READ) ||
		QUERY_IS_TYPE(qtype, QUERY_TYPE_USERVAR_READ) ||
		QUERY_IS_TYPE(qtype, QUERY_TYPE_SYSVAR_READ) ||
		QUERY_IS_TYPE(qtype, QUERY_TYPE_GSYSVAR_READ))
	{
                /**
                 * These values won't change qtype more restrictive than write.
                 * UDFs and procedures could possibly cause session-wide write,
                 * but unless their content is replicated this is a limitation
                 * of this implementation.
                 * In other words : UDFs and procedures are not allowed to
                 * perform writes which are not replicated but need to repeat
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
                        } 
                        else if (itype == Item::FUNC_ITEM) 
			{
                                int func_qtype = QUERY_TYPE_UNKNOWN;
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
                                        func_qtype |= QUERY_TYPE_WRITE;
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "functype FUNC_SP, stored proc "
                                                "or unknown function.",
                                                pthread_self())));
                                        break;
                                case Item_func::UDF_FUNC:
                                        func_qtype |= QUERY_TYPE_WRITE;
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "functype UDF_FUNC, user-defined "
                                                "function.",
                                                pthread_self())));
                                        break;
                                case Item_func::NOW_FUNC:
                                        func_qtype |= QUERY_TYPE_LOCAL_READ;
                                        LOGIF(LD, (skygw_log_write(
                                                LOGFILE_DEBUG,
                                                "%lu [resolve_query_type] "
                                                "functype NOW_FUNC, could be "
                                                "executed in MaxScale.",
                                                pthread_self())));
                                        break;
				/** System session variable */
				case Item_func::GSYSVAR_FUNC:
					func_qtype |= QUERY_TYPE_SYSVAR_READ;
					LOGIF(LD, (skygw_log_write(
						LOGFILE_DEBUG,
						"%lu [resolve_query_type] "
						"functype GSYSVAR_FUNC, system "
						"variable read.",
						pthread_self())));
					break;
					/** User-defined variable read */
				case Item_func::GUSERVAR_FUNC:
					func_qtype |= QUERY_TYPE_USERVAR_READ;
					LOGIF(LD, (skygw_log_write(
						LOGFILE_DEBUG,
						"%lu [resolve_query_type] "
						"functype GUSERVAR_FUNC, user "
						"variable read.",
						pthread_self())));
					break;
					/** User-defined variable modification */
				case Item_func::SUSERVAR_FUNC:
					/** 
					 * Really it is user variable but we 
					 * don't separate sql variables atm.
					 * 15.9.14
					 */
					func_qtype |= QUERY_TYPE_GSYSVAR_WRITE;
					LOGIF(LD, (skygw_log_write(
						LOGFILE_DEBUG,
						"%lu [resolve_query_type] "
						"functype SUSERVAR_FUNC, user "
						"variable write.",
						pthread_self())));
					break;
                                case Item_func::UNKNOWN_FUNC:
					if (item->name != NULL &&
						strcmp(item->name, "last_insert_id()") == 0)
					{
						func_qtype |= QUERY_TYPE_MASTER_READ;
					}
					else
					{
						func_qtype |= QUERY_TYPE_READ;
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
                                type |= set_query_type(&type, func_qtype);
                        }
#if defined(UPDATE_VAR_SUPPORT)
                        /**
                         * Write is as restrictive as it gets due functions,
                         * so break.
                         */
                        if ((type & QUERY_TYPE_WRITE) == QUERY_TYPE_WRITE) {
                                break;
                        }
#endif
                } /**< for */
	} /**< if */
return_qtype:
        qtype = (skygw_query_type_t)type;
        return qtype;
}

/**
 * Checks if statement causes implicit COMMIT.
 * autocommit_stmt gets values 1, 0 or -1 if stmt is enable, disable or 
 * something else than autocommit. 
 */
static bool skygw_stmt_causes_implicit_commit(
        LEX*  lex,
        int* autocommit_stmt)
{
        bool succp;

        if (!(sql_command_flags[lex->sql_command] & CF_AUTO_COMMIT_TRANS))
        {
                succp = false;
                goto return_succp;
        }
        
        switch (lex->sql_command) {
                case SQLCOM_DROP_TABLE:
                        succp = !(lex->drop_temporary);
                        break;
                case SQLCOM_ALTER_TABLE:
                case SQLCOM_CREATE_TABLE:
                        /* If CREATE TABLE of non-temporary table, do implicit commit */
                        succp = !(lex->create_info.options & HA_LEX_CREATE_TMP_TABLE);
                        break;
                case SQLCOM_SET_OPTION:
                        if ((*autocommit_stmt = is_autocommit_stmt(lex)) == 1)
                        {
                                succp = true;
                        }
                        else 
                        {
                                succp  =false;
                        }
                        break;
                default:
                        succp = true;
                        break;
        }
return_succp:
        return succp;
}


/**
 * Finds out if stmt is SET autocommit
 * and if the new value matches with the enable_cmd argument.
 * 
 * Returns 1, 0, or -1 if command was:
 * enable, disable, or not autocommit, respectively.
 */
static int is_autocommit_stmt(
        LEX* lex)
{
        struct list_node* node;
        set_var*          setvar;
        int               rc = -1;
        static char       target[8]; /*< for converted string */
        Item*             item = NULL;
        
        node = lex->var_list.first_node();
        setvar=(set_var*)node->info;
        
        if (setvar == NULL)
        {
                goto return_rc;
        }
       
        do /*< Search for the last occurrence of 'autocommit' */
        {
                if ((sys_var*)setvar->var == Sys_autocommit_ptr) 
                {
                        item = setvar->value;
                }
                node = node->next;
        } while ((setvar = (set_var*)node->info) != NULL);
        
        if (item != NULL) /*< found autocommit command */
        {
                if (item->type() == Item::INT_ITEM) /*< '0' or '1' */
                {
                        rc = item->val_int();
                        
                        if (rc > 1 || rc < 0)
                        {
                                rc = -1;
                        }
                }
                else if (item->type() == Item::STRING_ITEM) /*< 'on' or 'off' */
                {
                        String  str(target, sizeof(target), system_charset_info);
                        String* res = item->val_str(&str);
                        
                        if ((rc = find_type(&bool_typelib, res->ptr(), res->length(), false)))
                        {
                                ss_dassert(rc >= 0 && rc <= 2);
                                /** 
                                 * rc is the position of matchin string in 
                                 * typelib's value array. 
                                 * 1=OFF, 2=ON.
                                 */
                                rc -= 1;
                        }
                }
        }

return_rc:
        return rc;
}


char* skygw_query_classifier_get_stmtname(
        MYSQL* mysql)
{
        return ((THD *)(mysql->thd))->lex->prepared_stmt_name.str;
        
}

/**
 *Returns the LEX struct of the parsed GWBUF
 *@param The parsed GWBUF
 *@return Pointer to the LEX struct or NULL if an error occurred or the query was not parsed
 */
LEX* get_lex(GWBUF* querybuf)
{

  parsing_info_t* pi;
  MYSQL*          mysql;
  THD*            thd;
        
  if (!GWBUF_IS_PARSED(querybuf))
    {
      return NULL;
    }
  pi = (parsing_info_t *)gwbuf_get_buffer_object_data(querybuf, 
						      GWBUF_PARSING_INFO);

  if (pi == NULL)
    {
      return NULL;
    }
        
  if ((mysql = (MYSQL *)pi->pi_handle) == NULL || 
      (thd = (THD *)mysql->thd) == NULL)
    {
      ss_dassert(mysql != NULL && 
		 thd != NULL);
      return NULL;
    }

  return thd->lex;
}



/**
 * Finds the head of the list of tables affected by the current select statement.
 * @param thd Pointer to a valid THD
 * @return Pointer to the head of the TABLE_LIST chain or NULL in case of an error
 */
void* skygw_get_affected_tables(void* lexptr)
{
        LEX* lex = (LEX*)lexptr;
        
        if(lex == NULL ||
        lex->current_select == NULL)
        {
                ss_dassert(lex != NULL &&
                lex->current_select != NULL);
                return NULL;
        }

        return (void*)lex->current_select->table_list.first;
}


/**
 * Reads the parsetree and lists all the affected tables and views in the query.
 * In the case of an error, the size of the table is set to zero and no memory is allocated.
 * The caller must free the allocated memory.
 *
 * @param querybuf GWBUF where the table names are extracted from
 * @param tblsize Pointer where the number of tables is written
 * @return Array of null-terminated strings with the table names
 */
char** skygw_get_table_names(GWBUF* querybuf,int* tblsize, bool fullnames)
{
  LEX*			lex;
  TABLE_LIST*		tbl;
  int			i = 0,
			currtblsz = 0;
  char			**tables,
			**tmp;

  if((lex = get_lex(querybuf)) == NULL)
    {
      goto retblock;
    }        

  lex->current_select = lex->all_selects_list;    

  while(lex->current_select){
    
    tbl = (TABLE_LIST*)skygw_get_affected_tables(lex);

    while (tbl) 
      {
	if(i >= currtblsz){
	  
	  tmp = (char**)malloc(sizeof(char*)*(currtblsz*2+1));

	  if(tmp){
	    if(currtblsz > 0){
	      int x;
	      for(x = 0;x<currtblsz;x++){
		tmp[x] = tables[x]; 
	      }
	      free(tables);
	    }

	    tables = tmp;
	    currtblsz = currtblsz*2 + 1;

	  }	  
	  
	}

	char *catnm = NULL;

	if(fullnames)
	  {	    
	    if(tbl->db && strcmp(tbl->db,"skygw_virtual") != 0)
	      {
		catnm = (char*)calloc(strlen(tbl->db) + strlen(tbl->table_name) + 2,sizeof(char));
		strcpy(catnm,tbl->db);
		strcat(catnm,".");
		strcat(catnm,tbl->table_name);		
	      }	    
	  }
	
	if(catnm)
	  {
	    tables[i++] = catnm;
	  }
	else
	  {
	    tables[i++] = strdup(tbl->table_name);
	  }

	tbl=tbl->next_local;
      }
    lex->current_select = lex->current_select->next_select_in_list();
  }

 retblock:
  *tblsize = i;
  return tables;
}

/**
 * Extract, allocate memory and copy the name of the created table.
 * @param querybuf Buffer to use.
 * @return A pointer to the name if a table was created, otherwise NULL
 */
char* skygw_get_created_table_name(GWBUF* querybuf)
{
  LEX* lex;
  
  if((lex = get_lex(querybuf)) == NULL)
    {
      return NULL;
    }

  if(lex->create_last_non_select_table && 
     lex->create_last_non_select_table->table_name){
    char* name = strdup(lex->create_last_non_select_table->table_name);
    return name;
  }else{
    return NULL;
  }
  
}

/**
 * Checks whether the query is a "real" query ie. SELECT,UPDATE,INSERT,DELETE or any variation of these.
 * Queries that affect the underlying database are not considered as real queries and the queries that target
 * specific row or variable data are regarded as the real queries.
 * @param GWBUF to analyze
 * @return true if the query is a real query, otherwise false
 */
bool skygw_is_real_query(GWBUF* querybuf)
{
  LEX* lex = get_lex(querybuf);
  if(lex){
    switch(lex->sql_command){
    case SQLCOM_SELECT:
      return lex->all_selects_list->table_list.elements > 0;
    case SQLCOM_UPDATE:
    case SQLCOM_INSERT:
    case SQLCOM_INSERT_SELECT:
    case SQLCOM_DELETE:
    case SQLCOM_TRUNCATE:
    case SQLCOM_REPLACE:
    case SQLCOM_REPLACE_SELECT:
    case SQLCOM_PREPARE:
    case SQLCOM_EXECUTE:
      return true;
    default:
      return false;
	}
  }
  return false;
}


/**
 * Checks whether the buffer contains a DROP TABLE... query.
 * @param querybuf Buffer to inspect
 * @return true if it contains the query otherwise false
 */
bool is_drop_table_query(GWBUF* querybuf)
{
  LEX* lex;
        
  return (lex = get_lex(querybuf)) != NULL &&
	  lex->sql_command == SQLCOM_DROP_TABLE;
}

/*
 * Replace user-provided literals with question marks. Return a copy of the
 * querystr with replacements.
 * 
 * @param querybuf      GWBUF buffer including necessary parsing info
 * 
 * @return Copy of querystr where literals are replaces with question marks or
 * NULL if querystr is NULL, thread context or lex are NULL or if replacement
 * function fails.
 * 
 * Replaced literal types are STRING_ITEM,INT_ITEM,DECIMAL_ITEM,REAL_ITEM,
 * VARBIN_ITEM,NULL_ITEM
 */
char* skygw_get_canonical(
        GWBUF* querybuf)
{
        parsing_info_t* pi;
        MYSQL*          mysql;
        THD*            thd;
        LEX*            lex;
        Item*           item;
        char*           querystr;
        
        if (!GWBUF_IS_PARSED(querybuf))
        {
                querystr = NULL;
                goto retblock;
        }
        pi = (parsing_info_t *)gwbuf_get_buffer_object_data(querybuf, 
                                                            GWBUF_PARSING_INFO);
	CHK_PARSING_INFO(pi);
	
        if (pi == NULL)
        {
                querystr = NULL;
                goto retblock;                
        }
        
        if (pi->pi_query_plain_str == NULL || 
                (mysql = (MYSQL *)pi->pi_handle) == NULL || 
                (thd = (THD *)mysql->thd) == NULL ||
                (lex = thd->lex) == NULL)
        {
                ss_dassert(pi->pi_query_plain_str != NULL &&
                        mysql != NULL && 
                        thd != NULL && 
                        lex != NULL);
                querystr = NULL;
                goto retblock;
        }
        querystr = strdup(pi->pi_query_plain_str);

	for (item=thd->free_list; item != NULL; item=item->next) 
        {
                Item::Type itype;
                
		if (item->name == NULL)
		{
			continue;
		}
		itype = item->type();

		if (itype == Item::STRING_ITEM)
		{
			String tokenstr;
			String* res = item->val_str_ascii(&tokenstr);
			
			if (res->is_empty()) /*< empty string */
			{
				querystr = replace_literal(querystr, "\"\"", "\"?\"");
			}
			else
			{
				querystr = replace_literal(querystr, res->ptr(), "?");
			}
		}
		else if (itype == Item::INT_ITEM ||
			itype == Item::DECIMAL_ITEM ||
			itype == Item::REAL_ITEM ||
			itype == Item::VARBIN_ITEM ||
			itype == Item::NULL_ITEM)
		{
			querystr = replace_literal(querystr, item->name, "?");
		}
        } /*< for */
retblock:
        return querystr;
}


/**
 * Create parsing information; initialize mysql handle, allocate parsing info 
 * struct and set handle and free function pointer to it.
 * 
 * @param donefun       pointer to free function
 * 
 * @return pointer to parsing information
 */
parsing_info_t* parsing_info_init(
        void (*donefun)(void *))
{
        parsing_info_t* pi = NULL;
        MYSQL*          mysql;
        const char*     user  = "skygw";
        const char*     db    = "skygw";
        
        ss_dassert(donefun != NULL);
        
        /** Get server handle */
        mysql = mysql_init(NULL);
        ss_dassert(mysql != NULL);
        
        if (mysql == NULL) {
                LOGIF(LE, (skygw_log_write_flush(
                        LOGFILE_ERROR,
                        "Error : call to mysql_real_connect failed due %d, %s.",
                        mysql_errno(mysql),
			mysql_error(mysql))));
                
                goto retblock;
        }
        /** Set methods and authentication to mysql */
        mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, "libmysqld_skygw");
        mysql_options(mysql, MYSQL_OPT_USE_EMBEDDED_CONNECTION, NULL);
        mysql->methods = &embedded_methods;
        mysql->user    = my_strdup(user, MYF(0));
        mysql->db      = my_strdup(db, MYF(0));
        mysql->passwd  = NULL;
        
        pi = (parsing_info_t*)calloc(1, sizeof(parsing_info_t));
        
        if (pi == NULL)
        {
                mysql_close(mysql);
                goto retblock;
        }
#if defined(SS_DEBUG)
        pi->pi_chk_top  = CHK_NUM_PINFO;
        pi->pi_chk_tail = CHK_NUM_PINFO;
#endif
        /** Set handle and free function to parsing info struct */
        pi->pi_handle = mysql;
        pi->pi_done_fp = donefun;
        
retblock:
        return pi;
}

/**
 * Free function for parsing info. Called by gwbuf_free or in case initialization
 * of parsing information fails.
 * 
 * @param ptr Pointer to parsing information, cast required
 * 
 * @return void
 * 
 */
void parsing_info_done(
        void* ptr)
{
        parsing_info_t* pi = (parsing_info_t *)ptr;
        
        if (pi->pi_handle != NULL)
        {
                MYSQL* mysql = (MYSQL *)pi->pi_handle;
                
                if (mysql->thd != NULL)
                {
                        (*mysql->methods->free_embedded_thd)(mysql);
                        mysql->thd = NULL;
                }
                mysql_close(mysql);
        }
        /** Free plain text query string */
        if (pi->pi_query_plain_str != NULL)
        {
                free(pi->pi_query_plain_str);
        }
        free(pi);
}

/**
 * Add plain text query string to parsing info.
 * 
 * @param ptr   Pointer to parsing info struct, cast required
 * @param str   String to be added
 * 
 * @return void
 */
static void parsing_info_set_plain_str(
        void* ptr,
        char* str)
{
        parsing_info_t* pi = (parsing_info_t *)ptr;
        CHK_PARSING_INFO(pi);
        
        pi->pi_query_plain_str = str;
}
