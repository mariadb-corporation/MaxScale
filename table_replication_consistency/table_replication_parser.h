/*
Copyright (C) 2013-2014, MariaDB Corporation Ab


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

#ifndef TABLE_REPLICATION_PARSER_H
#define TABLE_REPLICATION_PARSER_H

namespace mysql {

namespace table_replication_parser {

/***********************************************************************//**
This function parses SQL-clauses and extracts table names
from the clause.
@return true if table names found, false if not
*/
bool
tbr_parser_table_names(
/*===================*/
        char **db_name,          /*!< inout: Array of db names */
	char **table_name,       /*!< inout: Array of table names */
	int *n_tables,           /*!< out: Number of db.table names found */
	const char* sql_string); /*!< in: SQL-clause */

} // table_replication_parser

} // mysql

#endif

