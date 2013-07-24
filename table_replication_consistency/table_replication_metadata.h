/*
Copyright (C) 2013, SkySQL Ab


This file is distributed as part of the SkySQL Gateway. It is free
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

Author: Jan Lindström jan.lindstrom@skysql.com
Created: 15-07-2013
Updated:
*/

#ifndef TABLE_REPLICATION_METADATA_H
#define TABLE_REPLICATION_METADATA_H

namespace mysql {

namespace table_replication_metadata {


/* Structure definition for table replication oconsistency metadata */
typedef struct {
	unsigned char* db_table;         /* Fully qualified db.table name,
					 primary key. */
	boost::uint32_t server_id;       /* Server id */
	unsigned char* gtid;             /* Global transaction id */
	boost::uint32_t gtid_len;        /* Length of gtid */
	boost::uint64_t binlog_pos;      /* Binlog position */
	bool gtid_known;                 /* Is gtid known ? */
} tbr_metadata_t;

/***********************************************************************//**
Read table replication consistency metadata from the MySQL master server.
This function assumes that necessary database and table are created.
@return false if read failed, true if read succeeded */
bool
tbrm_read_metadata(
/*===============*/
	const char *master_host,    /*!< in: Master hostname */
	const char *user,           /*!< in: username */
	const char *passwd,         /*!< in: password */
	unsigned int master_port,   /*!< in: master port */
	tbr_metadata_t **tbrm_meta, /*!< out: table replication consistency
				    metadata. */
	size_t *tbrm_rows);        /*!< out: number of rows read */

/***********************************************************************//**
Write table replication consistency metadata from the MySQL master server.
This function assumes that necessary database and table are created.
@return false if read failed, true if read succeeded */
bool
tbrm_write_metadata(
/*================*/
	const char *master_host,    /*!< in: Master hostname */
	const char *user,           /*!< in: username */
	const char *passwd,         /*!< in: password */
	unsigned int master_port,   /*!< in: master port */
	tbr_metadata_t **tbrm_meta, /*!< in: table replication consistency
				    metadata. */
	size_t tbrm_rows);          /*!< in: number of rows read */

} // table_replication_metadata

} // mysql

#endif


