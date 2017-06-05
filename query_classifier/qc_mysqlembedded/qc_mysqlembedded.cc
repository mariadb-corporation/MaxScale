/**
 * @section LICENCE
 *
 * This file is distributed as part of the MariaDB Corporation MaxScale. It is
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
 * Copyright MariaDB Corporation Ab
 *
 * @file
 *
 */

#define EMBEDDED_LIBRARY
#define MYSQL_YACC
#define MYSQL_LEX012
#define MYSQL_SERVER
#if defined(MYSQL_CLIENT)
#undef MYSQL_CLIENT
#endif
#include <my_config.h>
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
// In client_settings.h mysql_server_init and mysql_server_end are defined to
// mysql_client_plugin_init and mysql_client_plugin_deinit respectively.
// Those must be undefined, so that we here really call mysql_server_[init|end].
#undef mysql_server_init
#undef mysql_server_end
#include <set_var.h>
#include <strfunc.h>
#include <item_func.h>

#include <pthread.h>

#include <maxscale/debug.h>
#include <maxscale/log_manager.h>
#include <maxscale/platform.h>
#include <maxscale/query_classifier.h>
// <maxscale/protocol/mysql.h> assumes it is being compiled agains Connector-C,
// so we need to make certain Connector-C constants visible.
#define MYSQL_COM_QUIT        COM_QUIT
#define MYSQL_COM_INIT_DB     COM_INIT_DB
#define MYSQL_COM_CHANGE_USER COM_CHANGE_USER
#include <maxscale/protocol/mysql.h>
#include <maxscale/paths.h>
#include <maxscale/utils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/**
 * Defines what a particular name should be mapped to.
 */
typedef struct name_mapping
{
    const char* from;
    const char* to;
} NAME_MAPPING;

static NAME_MAPPING function_name_mappings_default[] =
{
    { NULL, NULL }
};

static NAME_MAPPING function_name_mappings_oracle[] =
{
    { "concat_operator_oracle", "concat" },
    { "case", "decode" },
    { NULL, NULL }
};

static const char* map_function_name(NAME_MAPPING* function_name_mappings, const char* from)
{
    NAME_MAPPING* map = function_name_mappings;
    const char* to = NULL;

    while (!to && map->from)
    {
        if (strcasecmp(from, map->from) == 0)
        {
            to = map->to;
        }
        else
        {
            ++map;
        }
    }

    return to ? to : from;
}

#define MYSQL_COM_QUERY_HEADER_SIZE 5 /*< 3 bytes size, 1 sequence, 1 command */
#define MAX_QUERYBUF_SIZE 2048
typedef struct parsing_info_st
{
#if defined(SS_DEBUG)
    skygw_chk_t pi_chk_top;
#endif
    void* pi_handle; /*< parsing info object pointer */
    char* pi_query_plain_str; /*< query as plain string */
    void (*pi_done_fp)(void *); /*< clean-up function for parsing info */
    QC_FIELD_INFO* field_infos;
    size_t field_infos_len;
    size_t field_infos_capacity;
    QC_FUNCTION_INFO* function_infos;
    size_t function_infos_len;
    size_t function_infos_capacity;
    GWBUF* preparable_stmt;
    qc_parse_result_t result;
    int32_t type_mask;
    NAME_MAPPING* function_name_mappings;
#if defined(SS_DEBUG)
    skygw_chk_t pi_chk_tail;
#endif
} parsing_info_t;

#define QTYPE_LESS_RESTRICTIVE_THAN_WRITE(t) (t<QUERY_TYPE_WRITE ? true : false)

static THD* get_or_create_thd_for_parsing(MYSQL* mysql, char* query_str);
static unsigned long set_client_flags(MYSQL* mysql);
static bool create_parse_tree(THD* thd);
static uint32_t resolve_query_type(parsing_info_t*, THD* thd);
static bool skygw_stmt_causes_implicit_commit(LEX* lex, int* autocommit_stmt);

static int is_autocommit_stmt(LEX* lex);
static parsing_info_t* parsing_info_init(void (*donefun)(void *));
static void parsing_info_set_plain_str(void* ptr, char* str);
/** Free THD context and close MYSQL */
static void parsing_info_done(void* ptr);
static TABLE_LIST* skygw_get_affected_tables(void* lexptr);
static bool ensure_query_is_parsed(GWBUF* query);
static bool parse_query(GWBUF* querybuf);
static bool query_is_parsed(GWBUF* buf);
int32_t qc_mysql_get_field_info(GWBUF* buf, const QC_FIELD_INFO** infos, uint32_t* n_infos);


#if MYSQL_VERSION_MAJOR >= 10 && MYSQL_VERSION_MINOR >= 3
inline void get_string_and_length(const LEX_CSTRING& ls, const char** s, size_t* length)
{
    *s = ls.str;
    *length = ls.length;
}
#else
inline void get_string_and_length(const char* cs, const char** s, size_t* length)
{
    *s = cs;
    *length = cs ? strlen(cs) : 0;
}
#endif

static struct
{
    qc_sql_mode_t   sql_mode;
    pthread_mutex_t sql_mode_mutex;
    NAME_MAPPING*   function_name_mappings;
} this_unit =
{
    QC_SQL_MODE_DEFAULT,
    PTHREAD_MUTEX_INITIALIZER,
    function_name_mappings_default
};

static thread_local struct
{
    qc_sql_mode_t sql_mode;
    NAME_MAPPING* function_name_mappings;
} this_thread =
{
    QC_SQL_MODE_DEFAULT,
    function_name_mappings_default
};

/**
 * Ensures that the query is parsed. If it is not already parsed, it
 * will be parsed.
 *
 * @return true if the query is parsed, false otherwise.
 */
bool ensure_query_is_parsed(GWBUF* query)
{
    bool parsed = query_is_parsed(query);

    if (!parsed)
    {
        // Instead of modifying global_system_variables, from which
        // thd->variables.sql_mode will be initialied, we should modify
        // thd->variables.sql_mode _after_ it has been created and
        // initialized.
        //
        // However, for whatever reason, the offset of that variable is
        // different when accessed from within libmysqld and qc_mysqlembedded,
        // so we will not modify the right variable even if it appears we do.
        //
        // So, for the time being we modify global_system_variables.sql_mode and
        // serialize the parsing. That's ok, since qc_mysqlembedded is only
        // used for verifying the behaviour of qc_sqlite.

        ss_debug(int rv);

        ss_debug(rv = )pthread_mutex_lock(&this_unit.sql_mode_mutex);
        ss_dassert(rv == 0);

        if (this_thread.sql_mode == QC_SQL_MODE_ORACLE)
        {
            global_system_variables.sql_mode |= MODE_ORACLE;
        }
        else
        {
            global_system_variables.sql_mode &= ~MODE_ORACLE;
        }

        parsed = parse_query(query);

        ss_debug(rv = )pthread_mutex_unlock(&this_unit.sql_mode_mutex);
        ss_dassert(rv == 0);

        if (!parsed)
        {
            MXS_ERROR("Unable to parse query, out of resources?");
        }
    }

    return parsed;
}

int32_t qc_mysql_parse(GWBUF* querybuf, uint32_t collect, int32_t* result)
{
    bool parsed = ensure_query_is_parsed(querybuf);

    // Since the query is parsed using the same parser - subject to version
    // differences between the embedded library and the server - either the
    // query is valid and hence correctly parsed, or the query is invalid in
    // which case the server will also consider it invalid and reject it. So,
    // it's always ok to claim it has been parsed.

    if (parsed)
    {
        parsing_info_t* pi = (parsing_info_t*) gwbuf_get_buffer_object_data(querybuf,
                                                                            GWBUF_PARSING_INFO);
        ss_dassert(pi);

        *result = pi->result;
    }
    else
    {
        *result = QC_QUERY_INVALID;
    }

    return QC_RESULT_OK;
}

int32_t qc_mysql_get_type_mask(GWBUF* querybuf, uint32_t* type_mask)
{
    int32_t rv = QC_RESULT_OK;

    *type_mask = QUERY_TYPE_UNKNOWN;
    MYSQL* mysql;
    bool succp;

    ss_info_dassert(querybuf != NULL, ("querybuf is NULL"));

    if (querybuf == NULL)
    {
        succp = false;
        goto retblock;
    }

    succp = ensure_query_is_parsed(querybuf);

    /** Read thd pointer and resolve the query type with it. */
    if (succp)
    {
        parsing_info_t* pi;

        pi = (parsing_info_t*) gwbuf_get_buffer_object_data(querybuf,
                                                            GWBUF_PARSING_INFO);

        if (pi != NULL)
        {
            mysql = (MYSQL *) pi->pi_handle;

            /** Find out the query type */
            if (mysql != NULL)
            {
                *type_mask = resolve_query_type(pi, (THD *) mysql->thd);
#if MYSQL_VERSION_MAJOR >= 10 && MYSQL_VERSION_MINOR >= 3
                // If in 10.3 mode we need to ensure that sequence related functions
                // are taken into account. That we can ensure by querying for the fields.
                const QC_FIELD_INFO* field_infos;
                uint32_t n_field_infos;

                rv = qc_mysql_get_field_info(querybuf, &field_infos, &n_field_infos);

                if (rv == QC_RESULT_OK)
                {
                    *type_mask |= pi->type_mask;
                }
#endif
            }
        }
    }

retblock:
    return rv;
}

/**
 * Create parsing info and try to parse the query included in the query buffer.
 * Store pointer to created parse_tree_t object to buffer.
 *
 * @param querybuf buffer including the query and possibly the parsing information
 *
 * @return true if succeed, false otherwise
 */
static bool parse_query(GWBUF* querybuf)
{
    bool succp;
    THD* thd;
    uint8_t* data;
    size_t len;
    char* query_str = NULL;
    parsing_info_t* pi;

    CHK_GWBUF(querybuf);
    /** Do not parse without releasing previous parse info first */
    ss_dassert(!query_is_parsed(querybuf));

    if (querybuf == NULL || query_is_parsed(querybuf))
    {
        MXS_ERROR("Query is NULL (%p) or query is already parsed.", querybuf);
        return false;
    }

    /** Create parsing info */
    pi = parsing_info_init(parsing_info_done);

    if (pi == NULL)
    {
        MXS_ERROR("Parsing info initialization failed.");
        succp = false;
        goto retblock;
    }

    /** Extract query and copy it to different buffer */
    data = (uint8_t*) GWBUF_DATA(querybuf);
    len = MYSQL_GET_PAYLOAD_LEN(data) - 1; /*< distract 1 for packet type byte */


    if (len < 1 || len >= ~((size_t) 0) - 1 || (query_str = (char *) malloc(len + 1)) == NULL)
    {
        /** Free parsing info data */
        MXS_ERROR("Length (%lu) is 0 or query string allocation failed (%p). Buffer is %lu bytes.",
                  len, query_str, GWBUF_LENGTH(querybuf));
        parsing_info_done(pi);
        succp = false;
        goto retblock;
    }

    memcpy(query_str, &data[5], len);
    memset(&query_str[len], 0, 1);
    parsing_info_set_plain_str(pi, query_str);

    /** Get one or create new THD object to be use in parsing */
    thd = get_or_create_thd_for_parsing((MYSQL *) pi->pi_handle, query_str);

    if (thd == NULL)
    {
        MXS_ERROR("THD creation failed.");
        /** Free parsing info data */
        parsing_info_done(pi);
        succp = false;
        goto retblock;
    }

    /**
     * Create parse_tree inside thd.
     * thd and lex are readable even if creating parse tree fails.
     */
    if (create_parse_tree(thd))
    {
        pi->result = QC_QUERY_PARSED;
    }

    /** Add complete parsing info struct to the query buffer */
    gwbuf_add_buffer_object(querybuf,
                            GWBUF_PARSING_INFO,
                            (void *) pi,
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
static bool query_is_parsed(GWBUF* buf)
{
    CHK_GWBUF(buf);
    return (buf != NULL && GWBUF_IS_PARSED(buf));
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
static THD* get_or_create_thd_for_parsing(MYSQL* mysql, char* query_str)
{
    THD* thd = NULL;
    unsigned long client_flags;
    char* db = mysql->options.db;
    bool failp = FALSE;
    size_t query_len;

    ss_info_dassert(mysql != NULL, ("mysql is NULL"));
    ss_info_dassert(query_str != NULL, ("query_str is NULL"));

    query_len = strlen(query_str);
    client_flags = set_client_flags(mysql);

    /** Get THD.
     * NOTE: Instead of creating new every time, THD instance could
     * be get from a pool of them.
     */
    thd = (THD *) create_embedded_thd(client_flags);

    if (thd == NULL)
    {
        MXS_ERROR("Failed to create thread context for parsing.");
        goto return_thd;
    }

    mysql->thd = thd;
    init_embedded_mysql(mysql, client_flags);
    failp = check_embedded_connection(mysql, db);

    if (failp)
    {
        MXS_ERROR("Call to check_embedded_connection failed.");
        goto return_err_with_thd;
    }

    thd->clear_data_list();

    /** Check that we are calling the client functions in right order */
    if (mysql->status != MYSQL_STATUS_READY)
    {
        set_mysql_error(mysql, CR_COMMANDS_OUT_OF_SYNC, unknown_sqlstate);
        MXS_ERROR("Invalid status %d in embedded server.",
                  mysql->status);
        goto return_err_with_thd;
    }

    /** Clear result variables */
    thd->current_stmt = NULL;
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
static unsigned long set_client_flags(MYSQL* mysql)
{
    unsigned long f = 0;

    f |= mysql->options.client_flag;

    /* Send client information for access check */
    f |= CLIENT_CAPABILITIES;

    if (f & CLIENT_MULTI_STATEMENTS)
    {
        f |= CLIENT_MULTI_RESULTS;
    }

    /**
     * No compression in embedded as we don't send any data,
     * and no pluggable auth, as we cannot do a client-server dialog
     */
    f &= ~(CLIENT_COMPRESS | CLIENT_PLUGIN_AUTH);

    if (mysql->options.db != NULL)
    {
        f |= CLIENT_CONNECT_WITH_DB;
    }

    return f;
}

static bool create_parse_tree(THD* thd)
{
    Parser_state parser_state;
    bool failp = FALSE;
    const char* virtual_db = "skygw_virtual";

    if (parser_state.init(thd, thd->query(), thd->query_length()))
    {
        failp = TRUE;
        goto return_here;
    }

    thd->reset_for_next_command();

    /**
     * Set some database to thd so that parsing won't fail because of
     * missing database. Then parse.
     */
    failp = thd->set_db(virtual_db, strlen(virtual_db));

    if (failp)
    {
        MXS_ERROR("Failed to set database in thread context.");
    }

    failp = parse_sql(thd, &parser_state, NULL);

    if (failp)
    {
        MXS_DEBUG("%lu [readwritesplit:create_parse_tree] failed to "
                  "create parse tree.",
                  pthread_self());
    }

return_here:
    return !failp;
}

/**
 * Sniff whether the statement is
 *
 *    SET ROLE ...
 *    SET NAMES ...
 *    SET PASSWORD ...
 *    SET CHARACTER ...
 *
 * Depending on what kind of SET statement it is, the parser of the embedded
 * library creates instances of set_var_user, set_var, set_var_password,
 * set_var_role, etc. that all are derived from set_var_base. However, there
 * is no type-information available in set_var_base, which is the type of the
 * instances when accessed from the lexer. Consequently, we cannot know what
 * kind of statment it is based on that, only whether it is a system variable
 * or not.
 *
 * Consequently, we just look at the string and deduce whether it is a
 * set [ROLE|NAMES|PASSWORD|CHARACTER] statement.
 */
bool is_set_specific(const char* s)
{
    bool rv = false;

    // Remove space from the beginning.
    while (isspace(*s))
    {
        ++s;
    }

    const char* token = s;

    // Find next non-space character.
    while (!isspace(*s) && (*s != 0))
    {
        ++s;
    }

    if (s - token == 3) // Might be "set"
    {
        if (strncasecmp(token, "set", 3) == 0)
        {
            // YES it was!
            while (isspace(*s))
            {
                ++s;
            }

            token = s;

            while (!isspace(*s) && (*s != 0) && (*s != '='))
            {
                ++s;
            }

            if (s - token == 4) // Might be "role"
            {
                if (strncasecmp(token, "role", 4) == 0)
                {
                    // YES it was!
                    rv = true;
                }
            }
            else if (s - token == 5) // Might be "names"
            {
                if (strncasecmp(token, "names", 5) == 0)
                {
                    // YES it was!
                    rv = true;
                }
            }
            else if (s - token == 8) // Might be "password
            {
                if (strncasecmp(token, "password", 8) == 0)
                {
                    // YES it was!
                    rv = true;
                }
            }
            else if (s - token == 9) // Might be "character"
            {
                if (strncasecmp(token, "character", 9) == 0)
                {
                    // YES it was!
                    rv = true;
                }
            }
        }
    }

    return rv;
}

/**
 * Detect query type by examining parsed representation of it.
 *
 * @param pi    The parsing info.
 * @param thd   MariaDB thread context.
 *
 * @return Copy of query type value.
 *
 *
 * @details Query type is deduced by checking for certain properties
 * of them. The order is essential. Some SQL commands have multiple
 * flags set and changing the order in which flags are tested,
 * the resulting type may be different.
 *
 */
static uint32_t resolve_query_type(parsing_info_t *pi, THD* thd)
{
    qc_query_type_t qtype = QUERY_TYPE_UNKNOWN;
    uint32_t type = QUERY_TYPE_UNKNOWN;
    int set_autocommit_stmt = -1; /*< -1 no, 0 disable, 1 enable */
    LEX* lex;
    Item* item;
    /**
     * By default, if sql_log_bin, that is, recording data modifications
     * to binary log, is disabled, gateway treats operations normally.
     * Effectively nothing is replicated.
     * When force_data_modify_op_replication is TRUE, gateway distributes
     * all write operations to all nodes.
     */
#if defined(NOT_IN_USE)
    bool force_data_modify_op_replication;
    force_data_modify_op_replication = FALSE;
#endif /* NOT_IN_USE */
    ss_info_dassert(thd != NULL, ("thd is NULL\n"));

    lex = thd->lex;

    /** SELECT ..INTO variable|OUTFILE|DUMPFILE */
    if (lex->result != NULL)
    {
        type = QUERY_TYPE_GSYSVAR_WRITE;
        goto return_qtype;
    }

    if (lex->describe)
    {
        type = QUERY_TYPE_READ;
        goto return_qtype;
    }

    if (skygw_stmt_causes_implicit_commit(lex, &set_autocommit_stmt))
    {
        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            if (sql_command_flags[lex->sql_command] &
                CF_IMPLICT_COMMIT_BEGIN)
            {
                MXS_INFO("Implicit COMMIT before executing the next command.");
            }
            else if (sql_command_flags[lex->sql_command] &
                     CF_IMPLICIT_COMMIT_END)
            {
                MXS_INFO("Implicit COMMIT after executing the next command.");
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
        if (MXS_LOG_PRIORITY_IS_ENABLED(LOG_INFO))
        {
            MXS_INFO("Disable autocommit : implicit START TRANSACTION"
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

        /*
         * SHOW GLOBAL STATUS - Route to master
         */
        else if (lex->sql_command == SQLCOM_SHOW_STATUS)
        {
            type = QUERY_TYPE_WRITE;
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
            if (is_set_specific(pi->pi_query_plain_str))
            {
                type |= QUERY_TYPE_GSYSVAR_WRITE;
            }
            else
            {
                List_iterator<set_var_base> ilist(lex->var_list);
                size_t n = 0;

                while (set_var_base *var = ilist++)
                {
                    if (var->is_system())
                    {
                        type |= QUERY_TYPE_GSYSVAR_WRITE;
                    }
                    else
                    {
                        type |= QUERY_TYPE_USERVAR_WRITE;
                    }
                    ++n;
                }

                if (n == 0)
                {
                    type |= QUERY_TYPE_GSYSVAR_WRITE;
                }
            }
        }
        else
        {
            type |= QUERY_TYPE_READ;
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
#if defined(NOT_IN_USE)

        if (thd->variables.sql_log_bin == 0 &&
            force_data_modify_op_replication)
        {
            /** Not replicated */
            type |= QUERY_TYPE_SESSION_WRITE;
        }
        else
#endif /* NOT_IN_USE */
        {
            /** Written to binlog, that is, replicated except tmp tables */
            type |= QUERY_TYPE_WRITE; /*< to master */

            if (lex->sql_command == SQLCOM_CREATE_TABLE &&
                (lex->create_info.options & HA_LEX_CREATE_TMP_TABLE))
            {
                type |= QUERY_TYPE_CREATE_TMP_TABLE; /*< remember in router */
            }
        }
    }

    /** Try to catch session modifications here */
    switch (lex->sql_command)
    {
    case SQLCOM_EMPTY_QUERY:
        type |= QUERY_TYPE_READ;
        break;

    case SQLCOM_CHANGE_DB:
        type |= QUERY_TYPE_SESSION_WRITE;
        break;

    case SQLCOM_DEALLOCATE_PREPARE:
        type |= QUERY_TYPE_WRITE;
        break;

    case SQLCOM_SELECT:
        type |= QUERY_TYPE_READ;
        break;

    case SQLCOM_CALL:
        type |= QUERY_TYPE_WRITE;
        break;

    case SQLCOM_BEGIN:
        type |= QUERY_TYPE_BEGIN_TRX;
        if (lex->start_transaction_opt & MYSQL_START_TRANS_OPT_READ_WRITE)
        {
            type |= QUERY_TYPE_WRITE;
        }
        else if (lex->start_transaction_opt & MYSQL_START_TRANS_OPT_READ_ONLY)
        {
            type |= QUERY_TYPE_READ;
        }
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

    case SQLCOM_SHOW_DATABASES:
        type |= QUERY_TYPE_SHOW_DATABASES;
        goto return_qtype;
        break;

    case SQLCOM_SHOW_TABLES:
        type |= QUERY_TYPE_SHOW_TABLES;
        goto return_qtype;
        break;

    case SQLCOM_SHOW_CREATE:
    case SQLCOM_SHOW_CREATE_DB:
    case SQLCOM_SHOW_CREATE_FUNC:
    case SQLCOM_SHOW_CREATE_PROC:
    case SQLCOM_SHOW_FIELDS:
    case SQLCOM_SHOW_FUNC_CODE:
    case SQLCOM_SHOW_GRANTS:
    case SQLCOM_SHOW_PROC_CODE:
    case SQLCOM_SHOW_SLAVE_HOSTS:
    case SQLCOM_SHOW_SLAVE_STAT:
    case SQLCOM_SHOW_STATUS:
        type |= QUERY_TYPE_READ;
        goto return_qtype;
        break;

    case SQLCOM_END:
        goto return_qtype;
        break;

    default:
        type |= QUERY_TYPE_WRITE;
        break;
    }

#if defined(UPDATE_VAR_SUPPORT)

    if (QTYPE_LESS_RESTRICTIVE_THAN_WRITE(type))
#endif
        // TODO: This test is meaningless, since at this point
        // TODO: qtype (not type) is QUERY_TYPE_UNKNOWN.
        if (qc_query_is_type(qtype, QUERY_TYPE_UNKNOWN) ||
            qc_query_is_type(qtype, QUERY_TYPE_LOCAL_READ) ||
            qc_query_is_type(qtype, QUERY_TYPE_READ) ||
            qc_query_is_type(qtype, QUERY_TYPE_USERVAR_READ) ||
            qc_query_is_type(qtype, QUERY_TYPE_SYSVAR_READ) ||
            qc_query_is_type(qtype, QUERY_TYPE_GSYSVAR_READ))
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
            for (item = thd->free_list; item != NULL; item = item->next)
            {
                Item::Type itype;

                itype = item->type();

                if (itype == Item::SUBSELECT_ITEM)
                {
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
                    ftype = ((Item_func*) item)->functype();

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
                    switch (ftype)
                    {
                    case Item_func::FUNC_SP:
                        /**
                         * An unknown (for maxscale) function / sp
                         * belongs to this category.
                         */
                        func_qtype |= QUERY_TYPE_WRITE;
                        MXS_DEBUG("%lu [resolve_query_type] "
                                  "functype FUNC_SP, stored proc "
                                  "or unknown function.",
                                  pthread_self());
                        break;

                    case Item_func::UDF_FUNC:
                        func_qtype |= QUERY_TYPE_WRITE;
                        MXS_DEBUG("%lu [resolve_query_type] "
                                  "functype UDF_FUNC, user-defined "
                                  "function.",
                                  pthread_self());
                        break;

                    case Item_func::NOW_FUNC:
                        // If this is part of a CREATE TABLE, then local read is not
                        // applicable.
                        if (lex->sql_command != SQLCOM_CREATE_TABLE)
                        {
                            func_qtype |= QUERY_TYPE_LOCAL_READ;
                            MXS_DEBUG("%lu [resolve_query_type] "
                                      "functype NOW_FUNC, could be "
                                      "executed in MaxScale.",
                                      pthread_self());
                        }
                        break;

                    /** System session variable */
                    case Item_func::GSYSVAR_FUNC:
                        {
                            const char* name;
                            size_t length;
                            get_string_and_length(item->name, &name, &length);

                            const char last_insert_id[] = "@@last_insert_id";
                            const char identity[] = "@@identity";

                            if (name &&
                                (((length == sizeof(last_insert_id) - 1) &&
                                  (strcasecmp(name, last_insert_id) == 0)) ||
                                 ((length == sizeof(identity) - 1) &&
                                  (strcasecmp(name, identity) == 0))))
                            {
                                func_qtype |= QUERY_TYPE_MASTER_READ;
                            }
                            else
                            {
                                func_qtype |= QUERY_TYPE_SYSVAR_READ;
                            }
                            MXS_DEBUG("%lu [resolve_query_type] "
                                      "functype GSYSVAR_FUNC, system "
                                      "variable read.",
                                      pthread_self());
                        }
                        break;

                    /** User-defined variable read */
                    case Item_func::GUSERVAR_FUNC:
                        func_qtype |= QUERY_TYPE_USERVAR_READ;
                        MXS_DEBUG("%lu [resolve_query_type] "
                                  "functype GUSERVAR_FUNC, user "
                                  "variable read.",
                                  pthread_self());
                        break;

                    /** User-defined variable modification */
                    case Item_func::SUSERVAR_FUNC:
                        func_qtype |= QUERY_TYPE_USERVAR_WRITE;
                        MXS_DEBUG("%lu [resolve_query_type] "
                                  "functype SUSERVAR_FUNC, user "
                                  "variable write.",
                                  pthread_self());
                        break;

                    case Item_func::UNKNOWN_FUNC:

                        if (((Item_func*) item)->func_name() != NULL &&
                            strcmp((char*) ((Item_func*) item)->func_name(), "last_insert_id") == 0)
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
                        MXS_DEBUG("%lu [resolve_query_type] "
                                  "functype UNKNOWN_FUNC, "
                                  "typically some system function.",
                                  pthread_self());
                        break;

                    default:
                        MXS_DEBUG("%lu [resolve_query_type] "
                                  "Functype %d.",
                                  pthread_self(),
                                  ftype);
                        break;
                    } /**< switch */

                    /**< Set new query type */
                    type |= func_qtype;
                }

#if defined(UPDATE_VAR_SUPPORT)

                /**
                 * Write is as restrictive as it gets due functions,
                 * so break.
                 */
                if ((type & QUERY_TYPE_WRITE) == QUERY_TYPE_WRITE)
                {
                    break;
                }

#endif
            } /**< for */
        } /**< if */

return_qtype:
    qtype = (qc_query_type_t) type;
    return qtype;
}

/**
 * Checks if statement causes implicit COMMIT.
 * autocommit_stmt gets values 1, 0 or -1 if stmt is enable, disable or
 * something else than autocommit.
 *
 * @param lex           Parse tree
 * @param autocommit_stmt   memory address for autocommit status
 *
 * @return true if statement causes implicit commit and false otherwise
 */
static bool skygw_stmt_causes_implicit_commit(LEX* lex, int* autocommit_stmt)
{
    bool succp;

    if (!(sql_command_flags[lex->sql_command] & CF_AUTO_COMMIT_TRANS))
    {
        succp = false;
        goto return_succp;
    }

    switch (lex->sql_command)
    {
    case SQLCOM_DROP_TABLE:
        succp = !(lex->create_info.options & HA_LEX_CREATE_TMP_TABLE);
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
            succp = false;
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
 * @param lex   parse tree
 *
 * @return 1, 0, or -1 if command was:
 * enable, disable, or not autocommit, respectively.
 */
static int is_autocommit_stmt(LEX* lex)
{
    struct list_node* node;
    set_var* setvar;
    int rc = -1;
    static char target[8]; /*< for converted string */
    Item* item = NULL;

    node = lex->var_list.first_node();
    setvar = (set_var*) node->info;

    if (setvar == NULL)
    {
        goto return_rc;
    }

    do /*< Search for the last occurrence of 'autocommit' */
    {
        if ((sys_var*) setvar->var == Sys_autocommit_ptr)
        {
            item = setvar->value;
        }

        node = node->next;
    }
    while ((setvar = (set_var*) node->info) != NULL);

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
            String str(target, sizeof (target), system_charset_info);
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

#if defined(NOT_USED)

char* qc_get_stmtname(GWBUF* buf)
{
    MYSQL* mysql;

    if (buf == NULL ||
        buf->gwbuf_bufobj == NULL ||
        buf->gwbuf_bufobj->bo_data == NULL ||
        (mysql = (MYSQL *) ((parsing_info_t *) buf->gwbuf_bufobj->bo_data)->pi_handle) == NULL ||
        mysql->thd == NULL ||
        (THD *) (mysql->thd))->lex == NULL ||
        (THD *) (mysql->thd))->lex->prepared_stmt_name == NULL)
    {
        return NULL;
    }

    return ((THD *) (mysql->thd))->lex->prepared_stmt_name.str;
}
#endif

/**
 * Get the parsing info structure from a GWBUF
 *
 * @param querybuf A GWBUF
 *
 * @return The parsing info object, or NULL
 */
parsing_info_t* get_pinfo(GWBUF* querybuf)
{
    parsing_info_t *pi = NULL;

    if ((querybuf != NULL) && GWBUF_IS_PARSED(querybuf))
    {
        pi = (parsing_info_t *) gwbuf_get_buffer_object_data(querybuf, GWBUF_PARSING_INFO);
    }

    return pi;
}

LEX* get_lex(parsing_info_t* pi)
{
    MYSQL* mysql = (MYSQL *) pi->pi_handle;
    ss_dassert(mysql);
    THD* thd = (THD *) mysql->thd;
    ss_dassert(thd);

    return thd->lex;
}

/**
 * Get the parse tree from parsed querybuf.
 * @param querybuf  The parsed GWBUF
 *
 * @return Pointer to the LEX struct or NULL if an error occurred or the query
 * was not parsed
 */
LEX* get_lex(GWBUF* querybuf)
{
    LEX* lex = NULL;
    parsing_info_t* pi = get_pinfo(querybuf);

    if (pi)
    {
        MYSQL* mysql = (MYSQL *) pi->pi_handle;
        ss_dassert(mysql);
        THD* thd = (THD *) mysql->thd;
        ss_dassert(thd);
        lex = thd->lex;
    }

    return lex;
}

/**
 * Finds the head of the list of tables affected by the current select statement.
 * @param thd Pointer to a valid THD
 * @return Pointer to the head of the TABLE_LIST chain or NULL in case of an error
 */
static TABLE_LIST* skygw_get_affected_tables(void* lexptr)
{
    LEX* lex = (LEX*) lexptr;

    if (lex == NULL || lex->current_select == NULL)
    {
        ss_dassert(lex != NULL && lex->current_select != NULL);
        return NULL;
    }

    TABLE_LIST *tbl = lex->current_select->table_list.first;

    if (tbl && tbl->schema_select_lex && tbl->schema_select_lex->table_list.elements &&
        lex->sql_command != SQLCOM_SHOW_KEYS)
    {
        /**
         * Some statements e.g. EXPLAIN or SHOW COLUMNS give `information_schema`
         * as the underlying table and the table in the query is stored in
         * @c schema_select_lex.
         *
         * SHOW [KEYS | INDEX] does the reverse so we need to skip the
         * @c schema_select_lex when processing a SHOW [KEYS | INDEX] statement.
         */
        tbl = tbl->schema_select_lex->table_list.first;
    }

    return tbl;
}

static bool is_show_command(int sql_command)
{
    bool rv = false;

    switch (sql_command)
    {
    case SQLCOM_SHOW_CREATE:
    case SQLCOM_SHOW_DATABASES:
    case SQLCOM_SHOW_FIELDS:
    case SQLCOM_SHOW_KEYS:
    case SQLCOM_SHOW_MASTER_STAT:
    case SQLCOM_SHOW_SLAVE_STAT:
    case SQLCOM_SHOW_STATUS:
    case SQLCOM_SHOW_TABLES:
    case SQLCOM_SHOW_TABLE_STATUS:
    case SQLCOM_SHOW_VARIABLES:
    case SQLCOM_SHOW_WARNS:
        rv = true;
        break;

    default:
        break;
    }

    return rv;
}

int32_t qc_mysql_get_table_names(GWBUF* querybuf, int32_t fullnames, char*** tablesp, int32_t* tblsize)
{
    LEX* lex;
    TABLE_LIST* tbl;
    int i = 0, currtblsz = 0;
    char **tables = NULL, **tmp = NULL;

    if (querybuf == NULL || tblsize == NULL)
    {
        goto retblock;
    }

    if (!ensure_query_is_parsed(querybuf))
    {
        goto retblock;
    }

    if ((lex = get_lex(querybuf)) == NULL)
    {
        goto retblock;
    }

    if (lex->describe || is_show_command(lex->sql_command))
    {
        goto retblock;
    }

    lex->current_select = lex->all_selects_list;

    while (lex->current_select)
    {
        tbl = skygw_get_affected_tables(lex);

        while (tbl)
        {
            if (i >= currtblsz)
            {
                tmp = (char**) malloc(sizeof (char*) * (currtblsz * 2 + 1));

                if (tmp)
                {
                    if (currtblsz > 0)
                    {
                        for (int x = 0; x < currtblsz; x++)
                        {
                            tmp[x] = tables[x];
                        }

                        free(tables);
                    }

                    tables = tmp;
                    currtblsz = currtblsz * 2 + 1;
                }
            }

            if (tmp != NULL)
            {
                char *catnm = NULL;

                if (fullnames)
                {
                    if (tbl->db &&
                        (strcmp(tbl->db, "skygw_virtual") != 0) &&
                        (strcmp(tbl->table_name, "*") != 0))
                    {
                        catnm = (char*) calloc(strlen(tbl->db) +
                                               strlen(tbl->table_name) +
                                               2,
                                               sizeof (char));
                        strcpy(catnm, tbl->db);
                        strcat(catnm, ".");
                        strcat(catnm, tbl->table_name);
                    }
                }

                if (catnm)
                {
                    tables[i++] = catnm;
                }
                else
                {
                    // Sometimes the tablename is "*"; we exclude that.
                    if (strcmp(tbl->table_name, "*") != 0)
                    {
                        tables[i++] = strdup(tbl->table_name);
                    }
                }

                tbl = tbl->next_local;
            }
        } /*< while (tbl) */

        lex->current_select = lex->current_select->next_select_in_list();
    } /*< while(lex->current_select) */

retblock:
    *tblsize = i;
    *tablesp = tables;

    return QC_RESULT_OK;
}

int32_t qc_mysql_get_created_table_name(GWBUF* querybuf, char** table_name)
{
    *table_name = NULL;

    if (querybuf == NULL)
    {
        return QC_RESULT_OK;
    }

    if (!ensure_query_is_parsed(querybuf))
    {
        return QC_RESULT_ERROR;
    }

    LEX* lex = get_lex(querybuf);

    if (lex && (lex->sql_command == SQLCOM_CREATE_TABLE))
    {
        if (lex->create_last_non_select_table &&
            lex->create_last_non_select_table->table_name)
        {
            *table_name = strdup(lex->create_last_non_select_table->table_name);
        }
    }

    return QC_RESULT_OK;
}

int32_t qc_mysql_is_drop_table_query(GWBUF* querybuf, int32_t* answer)
{
    *answer = 0;

    if (querybuf)
    {
        if (ensure_query_is_parsed(querybuf))
        {
            LEX* lex = get_lex(querybuf);

            *answer = lex && lex->sql_command == SQLCOM_DROP_TABLE;
        }
    }

    return QC_RESULT_OK;
}

int32_t qc_mysql_query_has_clause(GWBUF* buf, int32_t* has_clause)
{
    *has_clause = false;

    if (buf)
    {
        if (ensure_query_is_parsed(buf))
        {
            LEX* lex = get_lex(buf);

            if (lex)
            {
                if (!lex->describe && !is_show_command(lex->sql_command))
                {
                    SELECT_LEX* current = lex->all_selects_list;

                    while (current && !*has_clause)
                    {
                        if (current->where || current->having)
                        {
                            *has_clause = true;
                        }

                        current = current->next_select_in_list();
                    }
                }
            }
        }
    }

    return QC_RESULT_OK;
}

/**
 * Create parsing information; initialize mysql handle, allocate parsing info
 * struct and set handle and free function pointer to it.
 *
 * @param donefun       pointer to free function
 *
 * @return pointer to parsing information
 */
static parsing_info_t* parsing_info_init(void (*donefun)(void *))
{
    parsing_info_t* pi = NULL;
    MYSQL* mysql;
    const char* user = "skygw";
    const char* db = "skygw";

    ss_dassert(donefun != NULL);

    /** Get server handle */
    mysql = mysql_init(NULL);

    if (mysql == NULL)
    {
        MXS_ERROR("Call to mysql_real_connect failed due %d, %s.",
                  mysql_errno(mysql),
                  mysql_error(mysql));
        ss_dassert(mysql != NULL);
        goto retblock;
    }

    /** Set methods and authentication to mysql */
    mysql_options(mysql, MYSQL_READ_DEFAULT_GROUP, "libmysqld_skygw");
    mysql_options(mysql, MYSQL_OPT_USE_EMBEDDED_CONNECTION, NULL);
    mysql->methods = &embedded_methods;
    mysql->user = my_strdup(user, MYF(0));
    mysql->db = my_strdup(db, MYF(0));
    mysql->passwd = NULL;

    pi = (parsing_info_t*) calloc(1, sizeof (parsing_info_t));

    if (pi == NULL)
    {
        mysql_close(mysql);
        goto retblock;
    }

#if defined(SS_DEBUG)
    pi->pi_chk_top = CHK_NUM_PINFO;
    pi->pi_chk_tail = CHK_NUM_PINFO;
#endif
    /** Set handle and free function to parsing info struct */
    pi->pi_handle = mysql;
    pi->pi_done_fp = donefun;
    pi->result = QC_QUERY_INVALID;
    ss_dassert(this_thread.function_name_mappings);
    pi->function_name_mappings = this_thread.function_name_mappings;

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
static void parsing_info_done(void* ptr)
{
    parsing_info_t* pi;
    THD* thd;

    if (ptr)
    {
        pi = (parsing_info_t *) ptr;

        if (pi->pi_handle != NULL)
        {
            MYSQL* mysql = (MYSQL *) pi->pi_handle;

            if (mysql->thd != NULL)
            {
                thd = (THD*) mysql->thd;
                thd->end_statement();
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

        for (size_t i = 0; i < pi->field_infos_len; ++i)
        {
            free(pi->field_infos[i].database);
            free(pi->field_infos[i].table);
            free(pi->field_infos[i].column);
        }
        free(pi->field_infos);

        for (size_t i = 0; i < pi->function_infos_len; ++i)
        {
            free(pi->function_infos[i].name);
        }
        free(pi->function_infos);

        gwbuf_free(pi->preparable_stmt);

        free(pi);
    }
}

/**
 * Add plain text query string to parsing info.
 *
 * @param ptr   Pointer to parsing info struct, cast required
 * @param str   String to be added
 *
 * @return void
 */
static void parsing_info_set_plain_str(void* ptr, char* str)
{
    parsing_info_t* pi = (parsing_info_t *) ptr;
    CHK_PARSING_INFO(pi);

    pi->pi_query_plain_str = str;
}

int32_t qc_mysql_get_database_names(GWBUF* querybuf, char*** databasesp, int* size)
{
    LEX* lex;
    TABLE_LIST* tbl;
    char **databases = NULL, **tmp = NULL;
    int currsz = 0, i = 0;

    if (!querybuf)
    {
        goto retblock;
    }

    if (!ensure_query_is_parsed(querybuf))
    {
        goto retblock;
    }

    if ((lex = get_lex(querybuf)) == NULL)
    {
        goto retblock;
    }

    if (lex->describe || is_show_command(lex->sql_command))
    {
        goto retblock;
    }

    lex->current_select = lex->all_selects_list;

    while (lex->current_select)
    {
        tbl = lex->current_select->table_list.first;

        while (tbl)
        {
            if (lex->sql_command == SQLCOM_SHOW_FIELDS)
            {
                // If we are describing, we want the actual table, not the information_schema.
                if (tbl->schema_select_lex)
                {
                    tbl = tbl->schema_select_lex->table_list.first;
                }
            }

            // The database is sometimes an empty string. So as not to return
            // an array of empty strings, we need to check for that possibility.
            if ((strcmp(tbl->db, "skygw_virtual") != 0) && (*tbl->db != 0))
            {
                if (i >= currsz)
                {
                    tmp = (char**) realloc(databases,
                                           sizeof (char*) * (currsz * 2 + 1));

                    if (tmp == NULL)
                    {
                        goto retblock;
                    }

                    databases = tmp;
                    currsz = currsz * 2 + 1;
                }

                databases[i++] = strdup(tbl->db);
            }

            tbl = tbl->next_local;
        }

        lex->current_select = lex->current_select->next_select_in_list();
    }

retblock:
    *size = i;
    *databasesp = databases;

    return QC_RESULT_OK;
}

int32_t qc_mysql_get_operation(GWBUF* querybuf, int32_t* operation)
{
    *operation = QUERY_OP_UNDEFINED;

    if (querybuf)
    {
        if (ensure_query_is_parsed(querybuf))
        {
            LEX* lex = get_lex(querybuf);

            if (lex)
            {
                if (lex->describe)
                {
                    *operation = QUERY_OP_EXPLAIN;
                }
                else
                {
                    switch (lex->sql_command)
                    {
                    case SQLCOM_SELECT:
                        *operation = QUERY_OP_SELECT;
                        break;

                    case SQLCOM_CREATE_DB:
                    case SQLCOM_CREATE_EVENT:
                    case SQLCOM_CREATE_FUNCTION:
                    case SQLCOM_CREATE_INDEX:
                    case SQLCOM_CREATE_PROCEDURE:
#if MYSQL_VERSION_MAJOR >= 10 && MYSQL_VERSION_MINOR >= 3
                    case SQLCOM_CREATE_SEQUENCE:
#endif
                    case SQLCOM_CREATE_SERVER:
                    case SQLCOM_CREATE_SPFUNCTION:
                    case SQLCOM_CREATE_TABLE:
                    case SQLCOM_CREATE_TRIGGER:
                    case SQLCOM_CREATE_USER:
                    case SQLCOM_CREATE_VIEW:
                        *operation = QUERY_OP_CREATE;
                        break;

                    case SQLCOM_ALTER_DB:
                    case SQLCOM_ALTER_DB_UPGRADE:
                    case SQLCOM_ALTER_EVENT:
                    case SQLCOM_ALTER_FUNCTION:
                    case SQLCOM_ALTER_PROCEDURE:
                    case SQLCOM_ALTER_SERVER:
                    case SQLCOM_ALTER_TABLE:
                    case SQLCOM_ALTER_TABLESPACE:
                        *operation = QUERY_OP_ALTER;
                        break;

                    case SQLCOM_UPDATE:
                    case SQLCOM_UPDATE_MULTI:
                        *operation = QUERY_OP_UPDATE;
                        break;

                    case SQLCOM_INSERT:
                    case SQLCOM_INSERT_SELECT:
                    case SQLCOM_REPLACE:
                    case SQLCOM_REPLACE_SELECT:
                        *operation = QUERY_OP_INSERT;
                        break;

                    case SQLCOM_DELETE:
                    case SQLCOM_DELETE_MULTI:
                        *operation = QUERY_OP_DELETE;
                        break;

                    case SQLCOM_TRUNCATE:
                        *operation = QUERY_OP_TRUNCATE;
                        break;

                    case SQLCOM_DROP_DB:
                    case SQLCOM_DROP_EVENT:
                    case SQLCOM_DROP_FUNCTION:
                    case SQLCOM_DROP_INDEX:
                    case SQLCOM_DROP_PROCEDURE:
#if MYSQL_VERSION_MAJOR >= 10 && MYSQL_VERSION_MINOR >= 3
                    case SQLCOM_DROP_SEQUENCE:
#endif
                    case SQLCOM_DROP_SERVER:
                    case SQLCOM_DROP_TABLE:
                    case SQLCOM_DROP_TRIGGER:
                    case SQLCOM_DROP_USER:
                    case SQLCOM_DROP_VIEW:
                        *operation = QUERY_OP_DROP;
                        break;

                    case SQLCOM_CHANGE_DB:
                        *operation = QUERY_OP_CHANGE_DB;
                        break;

                    case SQLCOM_LOAD:
                        *operation = QUERY_OP_LOAD;
                        break;

                    case SQLCOM_GRANT:
                        *operation = QUERY_OP_GRANT;
                        break;

                    case SQLCOM_REVOKE:
                    case SQLCOM_REVOKE_ALL:
                        *operation = QUERY_OP_REVOKE;
                        break;

                    case SQLCOM_SHOW_CREATE:
                    case SQLCOM_SHOW_CREATE_DB:
                    case SQLCOM_SHOW_CREATE_FUNC:
                    case SQLCOM_SHOW_CREATE_PROC:
                    case SQLCOM_SHOW_DATABASES:
                    case SQLCOM_SHOW_FIELDS:
                    case SQLCOM_SHOW_FUNC_CODE:
                    case SQLCOM_SHOW_GRANTS:
                    case SQLCOM_SHOW_KEYS:
                    case SQLCOM_SHOW_MASTER_STAT:
                    case SQLCOM_SHOW_PROC_CODE:
                    case SQLCOM_SHOW_SLAVE_HOSTS:
                    case SQLCOM_SHOW_SLAVE_STAT:
                    case SQLCOM_SHOW_STATUS:
                    case SQLCOM_SHOW_TABLES:
                    case SQLCOM_SHOW_TABLE_STATUS:
                    case SQLCOM_SHOW_VARIABLES:
                    case SQLCOM_SHOW_WARNS:
                        *operation = QUERY_OP_SHOW;
                        break;

                    default:
                        *operation = QUERY_OP_UNDEFINED;
                    }
                }
            }
        }
    }

    return QC_RESULT_OK;
}

int32_t qc_mysql_get_prepare_name(GWBUF* stmt, char** namep)
{
    char* name = NULL;

    if (stmt)
    {
        if (ensure_query_is_parsed(stmt))
        {
            LEX* lex = get_lex(stmt);

            if (!lex->describe)
            {
                if ((lex->sql_command == SQLCOM_PREPARE) ||
                    (lex->sql_command == SQLCOM_EXECUTE) ||
                    (lex->sql_command == SQLCOM_DEALLOCATE_PREPARE))
                {
                    name = (char*)malloc(lex->prepared_stmt_name.length + 1);
                    if (name)
                    {
                        memcpy(name, lex->prepared_stmt_name.str, lex->prepared_stmt_name.length);
                        name[lex->prepared_stmt_name.length] = 0;
                    }
                }
            }
        }
    }

    *namep = name;

    return QC_RESULT_OK;
}

int32_t qc_mysql_get_preparable_stmt(GWBUF* stmt, GWBUF** preparable_stmt)
{
    if (stmt)
    {
        if (ensure_query_is_parsed(stmt))
        {
            LEX* lex = get_lex(stmt);

            if ((lex->sql_command == SQLCOM_PREPARE) && !lex->describe)
            {
                parsing_info_t* pi = get_pinfo(stmt);

                if (!pi->preparable_stmt)
                {
                    const char* preparable_stmt;
                    size_t preparable_stmt_len;
#if MYSQL_VERSION_MINOR >= 3
                    preparable_stmt = lex->prepared_stmt_code->name.str;
                    preparable_stmt_len = lex->prepared_stmt_code->name.length;
#else
                    preparable_stmt = lex->prepared_stmt_code.str;
                    preparable_stmt_len = lex->prepared_stmt_code.length;
#endif
                    size_t payload_len = preparable_stmt_len + 1;
                    size_t packet_len = MYSQL_HEADER_LEN + payload_len;

                    GWBUF* preperable_packet = gwbuf_alloc(packet_len);

                    if (preperable_packet)
                    {
                        // Encode the length of the payload in the 3 first bytes.
                        *((unsigned char*)GWBUF_DATA(preperable_packet) + 0) = payload_len;
                        *((unsigned char*)GWBUF_DATA(preperable_packet) + 1) = (payload_len >> 8);
                        *((unsigned char*)GWBUF_DATA(preperable_packet) + 2) = (payload_len >> 16);
                        // Sequence id
                        *((unsigned char*)GWBUF_DATA(preperable_packet) + 3) = 0x00;
                        // Payload, starts with command.
                        *((unsigned char*)GWBUF_DATA(preperable_packet) + 4) = COM_QUERY;
                        // Is followed by the statement.
                        char *s = (char*)GWBUF_DATA(preperable_packet) + 5;

                        // We copy the statment, blindly replacing all '?':s with '0':s as
                        // otherwise the parsing of the preparable statement as a regular
                        // statement will not always succeed.
                        const char* p = preparable_stmt;
                        while (p < preparable_stmt + preparable_stmt_len)
                        {
                            if (*p == '?')
                            {
                                *s = '0';
                            }
                            else
                            {
                                *s = *p;
                            }

                            ++p;
                            ++s;
                        }
                    }

                    pi->preparable_stmt = preperable_packet;
                }

                *preparable_stmt = pi->preparable_stmt;
            }
        }
    }

    return QC_RESULT_OK;
}

static bool should_exclude(const char* name, List<Item>* excludep)
{
    bool exclude = false;
    List_iterator<Item> ilist(*excludep);
    Item* exclude_item;

    while (!exclude && (exclude_item = ilist++))
    {
        const char* exclude_name;
        size_t length;
        get_string_and_length(exclude_item->name, &exclude_name, &length);

        if (exclude_name &&
            (strlen(name) == length) &&
            (strcasecmp(name, exclude_name) == 0))
        {
            exclude = true;
        }

        if (!exclude)
        {
            exclude_name = strrchr(exclude_item->full_name(), '.');

            if (exclude_name)
            {
                ++exclude_name; // Char after the '.'

                if (strcasecmp(name, exclude_name) == 0)
                {
                    exclude = true;
                }
            }
        }
    }

    return exclude;
}

static void add_field_info(parsing_info_t* info,
                           const char* database,
                           const char* table,
                           const char* column,
                           uint32_t usage,
                           List<Item>* excludep)
{
    ss_dassert(column);

    QC_FIELD_INFO item = { (char*)database, (char*)table, (char*)column, usage };

    size_t i;
    for (i = 0; i < info->field_infos_len; ++i)
    {
        QC_FIELD_INFO* field_info = info->field_infos + i;

        if (strcasecmp(item.column, field_info->column) == 0)
        {
            if (!item.table && !field_info->table)
            {
                ss_dassert(!item.database && !field_info->database);
                break;
            }
            else if (item.table && field_info->table && (strcmp(item.table, field_info->table) == 0))
            {
                if (!item.database && !field_info->database)
                {
                    break;
                }
                else if (item.database &&
                         field_info->database &&
                         (strcmp(item.database, field_info->database) == 0))
                {
                    break;
                }
            }
        }
    }

    QC_FIELD_INFO* field_infos = NULL;

    if (i == info->field_infos_len) // If true, the field was not present already.
    {
        // If only a column is specified, but not a table or database and we
        // have a list of expressions that should be excluded, we check if the column
        // value is present in that list. This is in order to exclude the second "d" in
        // a statement like "select a as d from x where d = 2".
        if (!(column && !table && !database && excludep && should_exclude(column, excludep)))
        {
            if (info->field_infos_len < info->field_infos_capacity)
            {
                field_infos = info->field_infos;
            }
            else
            {
                size_t capacity = info->field_infos_capacity ? 2 * info->field_infos_capacity : 8;
                field_infos = (QC_FIELD_INFO*)realloc(info->field_infos, capacity * sizeof(QC_FIELD_INFO));

                if (field_infos)
                {
                    info->field_infos = field_infos;
                    info->field_infos_capacity = capacity;
                }
            }
        }
    }
    else
    {
        info->field_infos[i].usage |= usage;
    }

    // If field_infos is NULL, then the field was found and has already been noted.
    if (field_infos)
    {
        item.database = item.database ? strdup(item.database) : NULL;
        item.table = item.table ? strdup(item.table) : NULL;
        ss_dassert(item.column);
        item.column = strdup(item.column);

        // We are happy if we at least could dup the column.

        if (item.column)
        {
            field_infos[info->field_infos_len++] = item;
        }
    }
}

static void add_function_info(parsing_info_t* info,
                              const char* name,
                              uint32_t usage)
{
    ss_dassert(name);

    name = map_function_name(info->function_name_mappings, name);

    QC_FUNCTION_INFO item = { (char*)name, usage };

    size_t i;
    for (i = 0; i < info->function_infos_len; ++i)
    {
        QC_FUNCTION_INFO* function_info = info->function_infos + i;

        if (strcasecmp(item.name, function_info->name) == 0)
        {
            break;
        }
    }

    QC_FUNCTION_INFO* function_infos = NULL;

    if (i == info->function_infos_len) // If true, the function was not present already.
    {
        if (info->function_infos_len < info->function_infos_capacity)
        {
            function_infos = info->function_infos;
        }
        else
        {
            size_t capacity = info->function_infos_capacity ? 2 * info->function_infos_capacity : 8;
            function_infos = (QC_FUNCTION_INFO*)realloc(info->function_infos,
                                                        capacity * sizeof(QC_FUNCTION_INFO));

            if (function_infos)
            {
                info->function_infos = function_infos;
                info->function_infos_capacity = capacity;
            }
        }
    }
    else
    {
        info->function_infos[i].usage |= usage;
    }

    // If function_infos is NULL, then the function was found and has already been noted.
    if (function_infos)
    {
        item.name = strdup(item.name);

        if (item.name)
        {
            function_infos[info->function_infos_len++] = item;
        }
    }
}

static void add_field_info(parsing_info_t* pi, Item_field* item, uint32_t usage, List<Item>* excludep)
{
    const char* database = item->db_name;
    const char* table = item->table_name;
    const char* s;
    size_t l;
    get_string_and_length(item->field_name, &s, &l);
    char column[l + 1];
    strncpy(column, s, l);
    column[l] = 0;

    LEX* lex = get_lex(pi);

    switch (lex->sql_command)
    {
    case SQLCOM_SHOW_FIELDS:
        if (!database)
        {
            database = "information_schema";
        }

        if (!table)
        {
            table = "COLUMNS";
        }
        break;

    case SQLCOM_SHOW_KEYS:
        if (!database)
        {
            database = "information_schema";
        }

        if (!table)
        {
            table = "STATISTICS";
        }
        break;

    case SQLCOM_SHOW_STATUS:
        if (!database)
        {
            database = "information_schema";
        }

        if (!table)
        {
            table = "SESSION_STATUS";
        }
        break;

    case SQLCOM_SHOW_TABLES:
        if (!database)
        {
            database = "information_schema";
        }

        if (!table)
        {
            table = "TABLE_NAMES";
        }
        break;

    case SQLCOM_SHOW_TABLE_STATUS:
        if (!database)
        {
            database = "information_schema";
        }

        if (!table)
        {
            table = "TABLES";
        }
        break;

    case SQLCOM_SHOW_VARIABLES:
        if (!database)
        {
            database = "information_schema";
        }

        if (!table)
        {
            table = "SESSION_STATUS";
        }
        break;

    default:
        break;
    }

    add_field_info(pi, database, table, column, usage, excludep);
}

static void add_field_info(parsing_info_t* pi, Item* item, uint32_t usage, List<Item>* excludep)
{
    const char* database = NULL;
    const char* table = NULL;
    const char* s;
    size_t l;
    get_string_and_length(item->name, &s, &l);
    char column[l + 1];
    strncpy(column, s, l);
    column[l] = 0;

    add_field_info(pi, database, table, column, usage, excludep);
}

typedef enum collect_source
{
    COLLECT_SELECT,
    COLLECT_WHERE,
    COLLECT_HAVING,
    COLLECT_GROUP_BY,
} collect_source_t;

static void update_field_infos(parsing_info_t* pi,
                               LEX* lex,
                               st_select_lex* select,
                               uint32_t usage,
                               List<Item>* excludep);

static void remove_surrounding_back_ticks(char* s)
{
    size_t len = strlen(s);

    if (*s == '`')
    {
        --len;
        memmove(s, s + 1, len);
        s[len] = 0;
    }

    if (s[len - 1] == '`')
    {
        s[len - 1] = 0;
    }
}

static bool should_function_be_ignored(parsing_info_t* pi, const char* func_name)
{
    bool rv = false;

    // We want to ignore functions that do not really appear as such in an
    // actual SQL statement. E.g. "SELECT @a" appears as a function "get_user_var".
    if ((strcasecmp(func_name, "decimal_typecast") == 0) ||
        (strcasecmp(func_name, "cast_as_char") == 0) ||
        (strcasecmp(func_name, "cast_as_date") == 0) ||
        (strcasecmp(func_name, "cast_as_datetime") == 0) ||
        (strcasecmp(func_name, "cast_as_time") == 0) ||
        (strcasecmp(func_name, "cast_as_signed") == 0) ||
        (strcasecmp(func_name, "cast_as_unsigned") == 0) ||
        (strcasecmp(func_name, "get_user_var") == 0) ||
        (strcasecmp(func_name, "get_system_var") == 0) ||
        (strcasecmp(func_name, "set_user_var") == 0) ||
        (strcasecmp(func_name, "set_system_var") == 0))
    {
        rv = true;
    }

    // Any sequence related functions should be ignored as well.
#if MYSQL_VERSION_MAJOR >= 10 && MYSQL_VERSION_MINOR >= 3
    if (!rv)
    {
        if ((strcasecmp(func_name, "lastval") == 0) ||
            (strcasecmp(func_name, "nextval") == 0))
        {
            pi->type_mask |= QUERY_TYPE_WRITE;
            rv = true;
        }
    }
#endif

    return rv;
}

static void update_field_infos(parsing_info_t* pi,
                               collect_source_t source,
                               Item* item,
                               uint32_t usage,
                               List<Item>* excludep)
{
    switch (item->type())
    {
    case Item::COND_ITEM:
        {
            Item_cond* cond_item = static_cast<Item_cond*>(item);
            List_iterator<Item> ilist(*cond_item->argument_list());

            while (Item *i = ilist++)
            {
                update_field_infos(pi, source, i, usage, excludep);
            }
        }
        break;

    case Item::FIELD_ITEM:
        add_field_info(pi, static_cast<Item_field*>(item), usage, excludep);
        break;

    case Item::REF_ITEM:
        {
            if (source != COLLECT_SELECT)
            {
                Item_ref* ref_item = static_cast<Item_ref*>(item);

                add_field_info(pi, item, usage, excludep);

                size_t n_items = ref_item->cols();

                for (size_t i = 0; i < n_items; ++i)
                {
                    Item* reffed_item = ref_item->element_index(i);

                    if (reffed_item != ref_item)
                    {
                        update_field_infos(pi, source, ref_item->element_index(i), usage, excludep);
                    }
                }
            }
        }
        break;

    case Item::ROW_ITEM:
        {
            Item_row* row_item = static_cast<Item_row*>(item);
            size_t n_items = row_item->cols();

            for (size_t i = 0; i < n_items; ++i)
            {
                update_field_infos(pi, source, row_item->element_index(i), usage, excludep);
            }
        }
        break;

    case Item::FUNC_ITEM:
    case Item::SUM_FUNC_ITEM:
        {
            Item_func* func_item = static_cast<Item_func*>(item);
            Item** items = func_item->arguments();
            size_t n_items = func_item->argument_count();

            // From comment in Item_func_or_sum(server/sql/item.h) abount the
            // func_name() member function:
            /*
              This method is used for debug purposes to print the name of an
              item to the debug log. The second use of this method is as
              a helper function of print() and error messages, where it is
              applicable. To suit both goals it should return a meaningful,
              distinguishable and sintactically correct string. This method
              should not be used for runtime type identification, use enum
              {Sum}Functype and Item_func::functype()/Item_sum::sum_func()
              instead.
              Added here, to the parent class of both Item_func and Item_sum.

              NOTE: for Items inherited from Item_sum, func_name() return part of
              function name till first argument (including '(') to make difference in
              names for functions with 'distinct' clause and without 'distinct' and
              also to make printing of items inherited from Item_sum uniform.
            */
            // However, we have no option but to use it.

            const char* f = func_item->func_name();

            char func_name[strlen(f) + 3 + 1]; // strlen(substring) - strlen(substr) from below.
            strcpy(func_name, f);
            trim(func_name); // Sometimes the embedded parser leaves leading and trailing whitespace.

            // Non native functions are surrounded by back-ticks, let's remove them.
            remove_surrounding_back_ticks(func_name);

            char* dot = strchr(func_name, '.');

            if (dot)
            {
                // If there is a dot in the name we assume we have something like
                // db.fn(). We remove the scope, can't return that in qc_sqlite
                ++dot;
                memmove(func_name, dot, strlen(func_name) - (dot - func_name) + 1);
                remove_surrounding_back_ticks(func_name);
            }

            char* parenthesis = strchr(func_name, '(');

            if (parenthesis)
            {
                // The func_name of count in "SELECT count(distinct ...)" is
                // "count(distinct", so we need to strip that away.
                *parenthesis = 0;
            }

            // We want to ignore functions that do not really appear as such in an
            // actual SQL statement. E.g. "SELECT @a" appears as a function "get_user_var".
            if (!should_function_be_ignored(pi, func_name))
            {
                if (strcmp(func_name, "%") == 0)
                {
                    // Embedded library silently changes "mod" into "%". We need to check
                    // what it originally was, so that the result agrees with that of
                    // qc_sqlite.
                    const char* s;
                    size_t l;
                    get_string_and_length(func_item->name, &s, &l);
                    if (s && (strncasecmp(s, "mod", 3) == 0))
                    {
                        strcpy(func_name, "mod");
                    }
                }
                else if (strcmp(func_name, "<=>") == 0)
                {
                    // qc_sqlite does not distinguish between "<=>" and "=", so we
                    // change "<=>" into "=".
                    strcpy(func_name, "=");
                }
                else if (strcasecmp(func_name, "substr") == 0)
                {
                    // Embedded library silently changes "substring" into "substr". We need
                    // to check what it originally was, so that the result agrees with
                    // that of qc_sqlite. We reserved space for this above.
                    const char* s;
                    size_t l;
                    get_string_and_length(func_item->name, &s, &l);
                    if (s && (strncasecmp(s, "substring", 9) == 0))
                    {
                        strcpy(func_name, "substring");
                    }
                }
                else if (strcasecmp(func_name, "add_time") == 0)
                {
                    // For whatever reason the name of "addtime" is returned as "add_time".
                    strcpy(func_name, "addtime");
                }

                add_function_info(pi, func_name, usage);
            }

            for (size_t i = 0; i < n_items; ++i)
            {
                update_field_infos(pi, source, items[i], usage, excludep);
            }
        }
        break;

    case Item::SUBSELECT_ITEM:
        {
            Item_subselect* subselect_item = static_cast<Item_subselect*>(item);

            switch (subselect_item->substype())
            {
            case Item_subselect::IN_SUBS:
                add_function_info(pi, "in", usage);
            case Item_subselect::ALL_SUBS:
            case Item_subselect::ANY_SUBS:
                {
                    Item_in_subselect* in_subselect_item = static_cast<Item_in_subselect*>(item);

#if (((MYSQL_VERSION_MAJOR == 5) &&\
      ((MYSQL_VERSION_MINOR > 5) ||\
       ((MYSQL_VERSION_MINOR == 5) && (MYSQL_VERSION_PATCH >= 48))\
      )\
     ) ||\
     (MYSQL_VERSION_MAJOR >= 10)\
    )
                    if (in_subselect_item->left_expr_orig)
                    {
                        update_field_infos(pi, source,
                                           in_subselect_item->left_expr_orig, usage, excludep);
                    }
                    st_select_lex* ssl = in_subselect_item->get_select_lex();
                    if (ssl)
                    {
                        uint32_t sub_usage = usage;

                        sub_usage &= ~QC_USED_IN_SELECT;
                        sub_usage |= QC_USED_IN_SUBSELECT;

                        update_field_infos(pi,
                                           get_lex(pi),
                                           ssl,
                                           sub_usage,
                                           excludep);
                    }
#else
#pragma message "Figure out what to do with versions < 5.5.48."
#endif
                    // TODO: Anything else that needs to be looked into?
                }
                break;

            case Item_subselect::EXISTS_SUBS:
                // TODO: Handle these explicitly as well.
                break;

            case Item_subselect::SINGLEROW_SUBS:
                {
                    Item_singlerow_subselect* ss_item = static_cast<Item_singlerow_subselect*>(item);
                    st_select_lex *ssl = ss_item->get_select_lex();

                    usage &= ~QC_USED_IN_SELECT;
                    usage |= QC_USED_IN_SUBSELECT;

                    update_field_infos(pi, get_lex(pi), ssl, usage, excludep);
                }
                break;

            case Item_subselect::UNKNOWN_SUBS:
            default:
                MXS_ERROR("Unknown subselect type: %d", subselect_item->substype());
                break;
            }
        }
        break;

    default:
        break;
    }
}

static void update_field_infos(parsing_info_t* pi,
                               LEX* lex,
                               st_select_lex* select,
                               uint32_t usage,
                               List<Item>* excludep)
{
    List_iterator<Item> ilist(select->item_list);

    while (Item *item = ilist++)
    {
        update_field_infos(pi, COLLECT_SELECT, item, usage, NULL);
    }

    if (select->group_list.first)
    {
        ORDER* order = select->group_list.first;
        while (order)
        {
            Item* item = *order->item;

            update_field_infos(pi, COLLECT_GROUP_BY, item, QC_USED_IN_GROUP_BY,
                               &select->item_list);

            order = order->next;
        }
    }

    if (select->where)
    {
        update_field_infos(pi, COLLECT_WHERE,
                           select->where,
                           QC_USED_IN_WHERE,
                           &select->item_list);
    }

#if defined(COLLECT_HAVING_AS_WELL)
    // A HAVING clause can only refer to fields that already have been
    // mentioned. Consequently, they need not be collected.
    if (select->having)
    {
        update_field_infos(pi, COLLECT_HAVING,
                           select->having,
                           0,
                           &select->item_list);
    }
#endif

    TABLE_LIST* table_list = select->get_table_list();

    if (table_list)
    {
        st_select_lex *sl = table_list->get_single_select();

        if (sl)
        {
            // This is for "SELECT 1 FROM (SELECT ...)"
            usage &= ~QC_USED_IN_SELECT;
            usage |= QC_USED_IN_SUBSELECT;
            update_field_infos(pi, get_lex(pi), sl, usage, excludep);
        }
    }
}

int32_t qc_mysql_get_field_info(GWBUF* buf, const QC_FIELD_INFO** infos, uint32_t* n_infos)
{
    *infos = NULL;
    *n_infos = 0;

    if (!buf)
    {
        return QC_RESULT_OK;
    }

    if (!ensure_query_is_parsed(buf))
    {
        return QC_RESULT_ERROR;
    }

    parsing_info_t* pi = get_pinfo(buf);
    ss_dassert(pi);

    if (!pi->field_infos)
    {
        LEX* lex = get_lex(buf);
        ss_dassert(lex);

        if (!lex)
        {
            return QC_RESULT_ERROR;
        }

        if (lex->describe || is_show_command(lex->sql_command))
        {
            *infos = NULL;
            *n_infos = 0;
            return QC_RESULT_OK;
        }

        uint32_t usage = 0;

        switch (lex->sql_command)
        {
        case SQLCOM_UPDATE:
        case SQLCOM_UPDATE_MULTI:
            usage |= QC_USED_IN_SET;
            break;

        default:
            usage |= QC_USED_IN_SELECT;
        }

        lex->current_select = &lex->select_lex;

        update_field_infos(pi, lex, &lex->select_lex, usage, NULL);

        List_iterator<Item> ilist(lex->value_list);
        while (Item* item = ilist++)
        {
            update_field_infos(pi, COLLECT_SELECT, item, 0, NULL);
        }

        if ((lex->sql_command == SQLCOM_INSERT) ||
            (lex->sql_command == SQLCOM_INSERT_SELECT) ||
            (lex->sql_command == SQLCOM_REPLACE) ||
            (lex->sql_command == SQLCOM_REPLACE_SELECT))
        {
            List_iterator<Item> ilist(lex->field_list);
            while (Item *item = ilist++)
            {
                update_field_infos(pi, COLLECT_SELECT, item, 0, NULL);
            }

            if (lex->insert_list)
            {
                List_iterator<Item> ilist(*lex->insert_list);
                while (Item *item = ilist++)
                {
                    update_field_infos(pi, COLLECT_SELECT, item, 0, NULL);
                }
            }
        }

        if (lex->sql_command == SQLCOM_SET_OPTION)
        {
#if defined(WAY_TO_DOWNCAST_SET_VAR_BASE_EXISTS)
            // The list of set_var_base contains the value of variables.
            // However, the actual type is a derived type of set_var_base
            // and there is no information using which we could do the
            // downcast...
            List_iterator<set_var_base> ilist(lex->var_list);
            while (set_var_base* var = ilist++)
            {
                // Is set_var_base a set_var, set_var_user, set_var_password
                // set_var_role
                ...
            }
#endif
            // ...so, we will simply assume that any nested selects are
            // from statements like "set @a:=(SELECT a from t1)".

            usage &= ~QC_USED_IN_SELECT;
            usage |= QC_USED_IN_SUBSELECT;

            st_select_lex* select = lex->all_selects_list;

            while (select)
            {
                if (select->nest_level != 0) // Not the top-level select.
                {
                    update_field_infos(pi, lex, select, usage, NULL);
                }

                select = select->next_select_in_list();
            }
        }
    }

    *infos = pi->field_infos;
    *n_infos = pi->field_infos_len;

    return QC_RESULT_OK;
}

int32_t qc_mysql_get_function_info(GWBUF* buf,
                                   const QC_FUNCTION_INFO** function_infos,
                                   uint32_t* n_function_infos)
{
    *function_infos = NULL;
    *n_function_infos = 0;

    int32_t rv = QC_RESULT_OK;

    if (buf)
    {
        const QC_FIELD_INFO* field_infos;
        uint32_t n_field_infos;

        // We ensure the information has been collected by querying the fields first.
        rv = qc_mysql_get_field_info(buf, &field_infos, &n_field_infos);

        if (rv == QC_RESULT_OK)
        {
            parsing_info_t* pi = get_pinfo(buf);
            ss_dassert(pi);

            *function_infos = pi->function_infos;
            *n_function_infos = pi->function_infos_len;
        }
    }

    return rv;
}

namespace
{

// Do not change the order without making corresponding changes to IDX_... below.
const char* server_options[] =
{
    "MariaDB Corporation MaxScale",
    "--no-defaults",
    "--datadir=",
    "--language=",
#if MYSQL_VERSION_MINOR < 3
    // TODO: 10.3 understands neither "--skip-innodb" or "--innodb=OFF", although it should.
    "--skip-innodb",
#endif
    "--default-storage-engine=myisam",
    NULL
};

const int IDX_DATADIR = 2;
const int IDX_LANGUAGE = 3;
const int N_OPTIONS = (sizeof(server_options) / sizeof(server_options[0])) - 1;

const char* server_groups[] =
{
    "embedded",
    "server",
    "server",
    "embedded",
    "server",
    "server",
    NULL
};

const int OPTIONS_DATADIR_SIZE = 10 + PATH_MAX; // strlen("--datadir=");
const int OPTIONS_LANGUAGE_SIZE = 11 + PATH_MAX; // strlen("--language=");

char datadir_arg[OPTIONS_DATADIR_SIZE];
char language_arg[OPTIONS_LANGUAGE_SIZE];


void configure_options(const char* datadir, const char* langdir)
{
    int rv;

    rv = snprintf(datadir_arg, OPTIONS_DATADIR_SIZE, "--datadir=%s", datadir);
    ss_dassert(rv < OPTIONS_DATADIR_SIZE); // Ensured by create_datadir().
    server_options[IDX_DATADIR] = datadir_arg;

    rv = sprintf(language_arg, "--language=%s", langdir);
    ss_dassert(rv < OPTIONS_LANGUAGE_SIZE); // Ensured by qc_process_init().
    server_options[IDX_LANGUAGE] = language_arg;

    // To prevent warning of unused variable when built in release mode,
    // when ss_dassert() turns into empty statement.
    (void)rv;
}

}

int32_t qc_mysql_setup(qc_sql_mode_t sql_mode, const char* zArgs)
{
    this_unit.sql_mode = sql_mode;

    if (sql_mode == QC_SQL_MODE_ORACLE)
    {
        this_unit.function_name_mappings = function_name_mappings_oracle;
    }

    if (zArgs)
    {
        MXS_WARNING("'%s' provided as arguments, "
                    "even though no arguments are supported.", zArgs);
    }

    return QC_RESULT_OK;
}

int32_t qc_mysql_process_init(void)
{
    bool inited = false;

    if (strlen(get_langdir()) >= PATH_MAX)
    {
        fprintf(stderr, "MaxScale: error: Language path is too long: %s.", get_langdir());
    }
    else
    {
        configure_options(get_process_datadir(), get_langdir());

        int argc = N_OPTIONS;
        char** argv = const_cast<char**>(server_options);
        char** groups = const_cast<char**>(server_groups);

        int rc = mysql_library_init(argc, argv, groups);

        if (rc != 0)
        {
            this_thread.sql_mode = this_unit.sql_mode;
            ss_dassert(this_unit.function_name_mappings);
            this_thread.function_name_mappings = this_unit.function_name_mappings;

            MXS_ERROR("mysql_library_init() failed. Error code: %d", rc);
        }
        else
        {
#if MYSQL_VERSION_ID >= 100000
            set_malloc_size_cb(NULL);
#endif
            MXS_NOTICE("Query classifier initialized.");
            inited = true;
        }
    }

    return inited ? QC_RESULT_OK : QC_RESULT_ERROR;
}

void qc_mysql_process_end(void)
{
    mysql_library_end();
}

int32_t qc_mysql_thread_init(void)
{
    this_thread.sql_mode = this_unit.sql_mode;
    ss_dassert(this_unit.function_name_mappings);
    this_thread.function_name_mappings = this_unit.function_name_mappings;

    bool inited = (mysql_thread_init() == 0);

    if (!inited)
    {
        MXS_ERROR("mysql_thread_init() failed.");
    }

    return inited ? QC_RESULT_OK : QC_RESULT_ERROR;
}

void qc_mysql_thread_end(void)
{
    mysql_thread_end();
}

int32_t qc_mysql_get_sql_mode(qc_sql_mode_t* sql_mode)
{
    *sql_mode = this_thread.sql_mode;
    return QC_RESULT_OK;
}

int32_t qc_mysql_set_sql_mode(qc_sql_mode_t sql_mode)
{
    int32_t rv = QC_RESULT_OK;

    switch (sql_mode)
    {
    case QC_SQL_MODE_DEFAULT:
        this_thread.sql_mode = sql_mode;
        this_thread.function_name_mappings = function_name_mappings_default;
        break;

    case QC_SQL_MODE_ORACLE:
        this_thread.sql_mode = sql_mode;
        this_thread.function_name_mappings = function_name_mappings_oracle;
        break;

    default:
        rv = QC_RESULT_ERROR;
    }

    return rv;
}

/**
 * EXPORTS
 */

extern "C"
{

    MXS_MODULE* MXS_CREATE_MODULE()
    {
        static QUERY_CLASSIFIER qc =
        {
            qc_mysql_setup,
            qc_mysql_process_init,
            qc_mysql_process_end,
            qc_mysql_thread_init,
            qc_mysql_thread_end,
            qc_mysql_parse,
            qc_mysql_get_type_mask,
            qc_mysql_get_operation,
            qc_mysql_get_created_table_name,
            qc_mysql_is_drop_table_query,
            qc_mysql_get_table_names,
            NULL,
            qc_mysql_query_has_clause,
            qc_mysql_get_database_names,
            qc_mysql_get_prepare_name,
            qc_mysql_get_field_info,
            qc_mysql_get_function_info,
            qc_mysql_get_preparable_stmt,
            qc_mysql_get_sql_mode,
            qc_mysql_set_sql_mode,
        };

        static MXS_MODULE info =
        {
            MXS_MODULE_API_QUERY_CLASSIFIER,
            MXS_MODULE_IN_DEVELOPMENT,
            QUERY_CLASSIFIER_VERSION,
            "Query classifier based upon MySQL Embedded",
            "V1.0.0",
            &qc,
            qc_mysql_process_init,
            qc_mysql_process_end,
            qc_mysql_thread_init,
            qc_mysql_thread_end,
            {
                {MXS_END_MODULE_PARAMS}
            }
        };

        return &info;
    }

}
