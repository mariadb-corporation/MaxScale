/*
Copyright (C) 2013, MariaDB Corporation Ab


This file is distributed as part of the MariaDB Corporation MaxScale. It is free
software: you can redistribute it and/or modify it under the terms of the
GNU General Public License as published by the Free Software Foundation,
version 2.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 51
Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

Author: Jan Lindström jan.lindstrom@mariadb.com

Created: 20-06-2013
Updated:

*/

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#include "table_replication_parser.h"
#include "table_replication_consistency.h"
#include "log_manager.h"

namespace mysql {

namespace table_replication_parser {

typedef struct {
        char* m_start;
        char* m_pos;
} tb_parser_t;

/***********************************************************************//**
This internal function initializes internal parser data structure based on
string to be parsed.*/
static void
tbr_parser_init(
/*============*/
	tb_parser_t* m,  /*!< inout: Parser structure to initialize */
	const char* s)         /*!< in: String to parse */
{
        m->m_start = (char *)s;
        m->m_pos = (char *)s;
}

/***********************************************************************//**
This internal function skips all space characters on front
@return position on string with next non space character*/
static char* 
tbr_parser_skipwspc(
	char* str)  /*!< in string */
{
        while (isspace(*str)) {
            str++;
        }
        return(str);
}


/***********************************************************************//**
This internal function parses input string and tries to match it to the given keyword.
@return true if next keyword matches, false if not
*/
static bool
tbr_match_keyword(
/*==============*/
	tb_parser_t* m,        /*!< inout: Parser structure */
	const char* const_str) /*!< in: Keyword to match    */
{
        size_t len;

	m->m_pos = tbr_parser_skipwspc(m->m_pos);

        if (const_str[0] == '\0') {
            return(m->m_pos[0] == '\0');
        }

        len = strlen(const_str);

	// Parsing is based on comparing two srings ignoring case
        if (strncasecmp(m->m_pos, const_str, len) == 0) {
		unsigned char c = (unsigned char)m->m_pos[len];
		if (isascii(c)) {
			if (isalnum(c)) {
				return (true);
			}
			if (c == '_') {
				return (false);
			}
		}
		m->m_pos += len;
		return(true);
        }
        return(false);
}

/***********************************************************************//**
Internal function to parse next quoted string
@return true if quoted string found, false if not
*/
static bool
tbr_get_quoted(
/*===========*/
        tb_parser_t* m,   /*!< inout: Parser structure */
        char* buf,        /*!< out: parsed string */
        unsigned int size,/*!< in: buffer size */
        bool keep_quotes) /*!< in: is quotes left on string */
{
        char quote;
        tb_parser_t saved_m;

	m->m_pos = tbr_parser_skipwspc(m->m_pos);

        saved_m = *m;

        quote = *m->m_pos++;

        if (keep_quotes) {
		*buf++ = quote;
		size--;
        }

        while (*m->m_pos != '\0') {
		if (*m->m_pos == quote) {
			if ((m->m_pos)[1] == quote) {
				m->m_pos++;

				if (keep_quotes) {
					*buf++ = quote;

					if (size-- <= 1) {
						*m = saved_m;
						return(false);
					}
				}
			} else {
				break;
			}
		}

		*buf++ = *m->m_pos++;

		if (size-- <= 1) {
			*m = saved_m;
			return(true);
		}
        }

        if (*m->m_pos != quote) {
		*m = saved_m;
		return(false);
        }

        m->m_pos++;

        if (keep_quotes) {
		*buf++ = quote;

		if (size-- <= 1) {
			*m = saved_m;
			return(true);
		}
        }

        *buf = '\0';

        return(true);
}

/***********************************************************************//**
This internal function parses identifiers e.g. table name
@return true if identifier is found, false if not
*/
static bool
tbr_get_id(
/*=======*/
        tb_parser_t* m, /*!< intout: Parser structure */
        char* id_buf,   /*!< out: parsed identifier */
        unsigned int id_size) /*!< in: identifier size */
{
        char* org_id_buf = id_buf;
        tb_parser_t saved_m;

	m->m_pos = tbr_parser_skipwspc(m->m_pos);
        saved_m = *m;

        if (*m->m_pos == '"' || *m->m_pos == '`') {
		if (!tbr_get_quoted(m, id_buf, id_size, false)) {
			*m = saved_m;
			return(false);
		}
        } else {
		while (isalnum(*m->m_pos) || *m->m_pos == '_') {
			*id_buf++ = *m->m_pos++;

			if (id_size-- <= 1) {
				*m = saved_m;
				return(true);
			}
		}
		*id_buf = '\0';
        }

        if (strlen(org_id_buf) > 0) {
		return(true);
        } else {
		*m = saved_m;
		return(false);
        }
}

/***********************************************************************//**
This internal function parses constants e.g. "."
@return true if constant is found, false if not
*/
static bool
tbr_match_const(
/*============*/
	tb_parser_t* m,  /*!< inout: Parser structure */
	const char* const_str) /*!< in: constant to be parsed */
{
        size_t len;

	m->m_pos = tbr_parser_skipwspc(m->m_pos);

        if (const_str[0] == '\0') {
		return(m->m_pos[0] == '\0');
        }

        len = strlen(const_str);

        if (strncasecmp(m->m_pos, const_str, len) == 0) {
		m->m_pos += len;
		return(true);
        } else {
		return(false);
        }
}

/***********************************************************************//**
This internal function skips to position where given keyword is found
@return true if keyword is found, false if not
*/
static bool
tbr_skipto_keyword(
/*===============*/
        tb_parser_t* m,       /*!< inout: Parser structure */
        const char* const_str,/*!< in: keyword to find*/
        const char* end_str)  /*!< in: stop at this keyword */
{
        size_t len;
        bool more = true;

	m->m_pos = tbr_parser_skipwspc(m->m_pos);

        if (const_str[0] == '\0') {
		return(m->m_pos[0] == '\0');
        }

        len = strlen(const_str);

        while (more) {
		if (strncasecmp(m->m_pos, const_str, len) == 0) {
			m->m_pos += len;
			return(true);
		} else {
			if(!(tbr_match_const(m, (char *)end_str))) {
				m->m_pos++;

				if (*(m->m_pos) == '\0'){
					return (false);
				}
			} else {
				m->m_pos-=strlen(end_str);
				return (false);
			}
		}
        }

        return(true);
}

/***********************************************************************//**
This internal function parses table name consisting database + "." + table
@return true if table name is found, false if not
*/
static bool
tbr_get_tablename(
/*==============*/
        tb_parser_t* m,            /*!< inout: Parser structure */
        char* dbname_buf,          /*!< out: Database name or empty string */
        size_t dbname_size,        /*!< in: size of db buffer */
        char* tabname_buf,         /*!< out: Tablename or empty string */
        size_t tabname_size)       /*!< in: size of tablename buffer */
{
        tb_parser_t saved_m;

        saved_m = *m;

	/* Try to parse database name */
        if (!tbr_get_id(m, dbname_buf, dbname_size)) {
		return(false);
        }

	/* If string does not contain constant "." there is no database name */
        if (!tbr_match_const(m, (char *)".")) {
		*m = saved_m;
		dbname_buf[0] = '\0';

		if (!tbr_get_id(m, tabname_buf, tabname_size)) {
			return(false);
		}
		return(true);
        }

	/* Try to parser table name */
        if (!tbr_get_id(m, tabname_buf, tabname_size)) {
		return(false);
        }

        return(true);
}

/***********************************************************************//**
This function parses SQL-clauses and extracts table names
from the clause.
@return true if table names found, false if not
*/
bool
tbr_parser_table_names(
/*===================*/
        char **db_name,         /*!< inout: Array of db names */
	char **table_name,      /*!< inout: Array of table names */
	int *n_tables,          /*!< out: Number of db.table names found */
	const char* sql_string) /*!< in: SQL-clause */
{
        tb_parser_t m;
	size_t name_count=0;
	char *dbname=NULL;
	char *tbname=NULL;
	size_t len = strlen(sql_string);

	tbr_parser_init(&m, sql_string);
	*n_tables = 0;

	// MySQL does not support multi-table insert or replace
	if ((tbr_match_keyword(&m, "INSERT") || tbr_match_keyword(&m, "REPLACE")) &&
		tbr_skipto_keyword(&m, "INTO", "")) {
		dbname = (char *)malloc(len+1);
		tbname = (char *)malloc(len+1);

		if (tbr_get_tablename(&m, dbname, len, tbname, len)) {
			db_name[name_count] = dbname;
			table_name[name_count] = tbname;
			name_count++;

			if (tbr_debug) {
				skygw_log_write_flush( LOGFILE_TRACE,
					(char *)"TRC Debug: INSERT OR REPLACE to %s.%s",
					dbname, tbname);
			}
		} else {
			free(dbname);
			free(tbname);
			return (false); // Parse error
		}
	}
	// MySQL does support multi table delete/update
	if ((tbr_match_keyword(&m, "DELETE") &&
		tbr_skipto_keyword(&m, "FROM","")) ||
		(tbr_match_keyword(&m, "UPDATE"))) {
		dbname = (char *)malloc(len+1);
		tbname = (char *)malloc(len+1);

		// These will eat the optional keywords from update
		tbr_match_keyword(&m, "LOW PRIORITY");
		tbr_match_keyword(&m, "IGNORE");

		// Parse the first db.table name
		if (tbr_get_tablename(&m, dbname, len,tbname,len)) {
			db_name[name_count] = dbname;
			table_name[name_count] = tbname;
			name_count++;

			// Table names are delimited by ","
			while(tbr_match_const(&m, ",")) {
				dbname = (char *)malloc(len+1);
				tbname = (char *)malloc(len+1);
				// Parse the next db.table name
				if (tbr_get_tablename(&m, dbname, len,tbname,len)) {
					db_name[name_count] = dbname;
					table_name[name_count] = tbname;
					name_count++;

					if (tbr_debug) {
						skygw_log_write_flush( LOGFILE_TRACE,
							(char *)"TRC Debug: DELETE OR UPDATE to %s.%s",
							dbname, tbname);
					}
				} else {
					free(dbname);
					free(tbname);
					return (false);
				}
			}
		}
	}

	// LOAD command
	if (tbr_match_keyword(&m, "LOAD") &&
		tbr_skipto_keyword(&m, "INTO", "")) {

		// Eat TABLE keyword
		tbr_match_keyword(&m, "TABLE");

		dbname = (char *)malloc(len+1);
		tbname = (char *)malloc(len+1);

		if (tbr_get_tablename(&m, dbname, len, tbname, len)) {
			db_name[name_count] = dbname;
			table_name[name_count] = tbname;
			name_count++;

			if (tbr_debug) {
				skygw_log_write_flush( LOGFILE_TRACE,
					(char *)"TRC Debug: LOAD to %s.%s",
					dbname, tbname);
			}
		} else {
			free(dbname);
			free(tbname);
			return (false); // Parse error
		}
	}

	// Create/Drop table
	if (tbr_match_keyword(&m, "CREATE") &&
		tbr_skipto_keyword(&m, "DROP", "")) {

		// Eat TEMPORARY keyword
		tbr_match_keyword(&m, "TEMPORARY");

		// Eat IF NOT EXISTS
		tbr_match_keyword(&m, "IF NOT EXISTS");

		// Eat IF EXISTS
		tbr_match_keyword(&m, "IF EXISTS");

		// Eat TABLE keyword
		tbr_match_keyword(&m, "TABLE");

		dbname = (char *)malloc(len+1);
		tbname = (char *)malloc(len+1);

		if (tbr_get_tablename(&m, dbname, len, tbname, len)) {
			db_name[name_count] = dbname;
			table_name[name_count] = tbname;
			name_count++;

			if (tbr_debug) {
			// Table names are delimited by ","
			while(tbr_match_const(&m, ",")) {
				dbname = (char *)malloc(len+1);
				tbname = (char *)malloc(len+1);
				// Parse the next db.table name
				if (tbr_get_tablename(&m, dbname, len,tbname,len)) {
					db_name[name_count] = dbname;
					table_name[name_count] = tbname;
					name_count++;

					if (tbr_debug) {
						skygw_log_write_flush( LOGFILE_TRACE,
							(char *)"TRC Debug: DROP TABLE to %s.%s",
							dbname, tbname);
					}
				} else {
					free(dbname);
					free(tbname);
					return (false);
				}
			}
				skygw_log_write_flush( LOGFILE_TRACE,
					(char *)"TRC Debug: CREATE/DROP TABLE to %s.%s",
					dbname, tbname);
			}
		} else {
			free(dbname);
			free(tbname);
			return (false); // Parse error
		}
	}


	*n_tables = name_count;

	if (name_count == 0) {
		return (false); // Parse error
	}

	return (true);
}

} // table_replication_parser

} // mysql

