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
Created: 15-07-2013
Updated:
*/

#ifndef TABLE_REPLICATION_METADATA_H
#define TABLE_REPLICATION_METADATA_H

namespace mysql {

namespace table_replication_metadata {


/* Structure definition for table replication consistency metadata */
typedef struct {
	unsigned char* db_table;         /* Fully qualified db.table name,
					 primary key. */
	boost::uint32_t server_id;       /* Server id */
	unsigned char* gtid;             /* Global transaction id */
	boost::uint32_t gtid_len;        /* Length of gtid */
	boost::uint64_t binlog_pos;      /* Binlog position */
	bool gtid_known;                 /* Is gtid known ? */
} tbr_metadata_t;

/* Structure definition for table replication server metadata */
typedef struct {
	boost::uint32_t server_id;       /* Server id, primary key*/
	boost::uint64_t binlog_pos;      /* Last executed binlog position */
	unsigned char* gtid;             /* Last executed global transaction
					 id if known */
	boost::uint32_t gtid_len;        /* Actual length of gtid */
	bool gtid_known;                 /* 1 if gtid known, 0 if not */
	boost::uint32_t server_type;     /* server type */
 } tbr_server_t;

// Not really nice, but currently we support only these two
// server types.
enum trc_server_type { TRC_SERVER_TYPE_MARIADB = 1, TRC_SERVER_TYPE_MYSQL = 2 };


/***********************************************************************//**
Read table replication consistency metadata from the MySQL master server.
This function assumes that necessary database and table are created.
@return false if read failed, true if read succeeded */
bool
tbrm_read_consistency_metadata(
/*===========================*/
	const char *master_host,    /*!< in: Master hostname */
	const char *user,           /*!< in: username */
	const char *passwd,         /*!< in: password */
	unsigned int master_port,   /*!< in: master port */
	tbr_metadata_t **tbrm_meta, /*!< out: table replication consistency
				    metadata. */
	size_t *tbrm_rows);        /*!< out: number of rows read */

/***********************************************************************//**
Read table replication server metadata from the MySQL master server.
This function assumes that necessary database and table are created.
@return false if read failed, true if read succeeded */
bool
tbrm_read_server_metadata(
/*======================*/
	const char *master_host,    /*!< in: Master hostname */
	const char *user,           /*!< in: username */
	const char *passwd,         /*!< in: password */
	unsigned int master_port,   /*!< in: master port */
	tbr_server_t **tbrm_server, /*!< out: table replication server
				    metadata. */
	size_t *tbrm_rows);        /*!< out: number of rows read */

/***********************************************************************//**
Write table replication consistency metadata from the MySQL master server.
This function assumes that necessary database and table are created.
@return false if read failed, true if read succeeded */
bool
tbrm_write_consistency_metadata(
/*============================*/
	const char *master_host,    /*!< in: Master hostname */
	const char *user,           /*!< in: username */
	const char *passwd,         /*!< in: password */
	unsigned int master_port,   /*!< in: master port */
	tbr_metadata_t **tbrm_meta, /*!< in: table replication consistency
				    metadata. */
	size_t tbrm_rows);          /*!< in: number of rows read */

/***********************************************************************//**
Write table replication server metadata from the MySQL master server.
This function assumes that necessary database and table are created.
@return false if read failed, true if read succeeded */
bool
tbrm_write_server_metadata(
/*=======================*/
	const char *master_host,    /*!< in: Master hostname */
	const char *user,           /*!< in: username */
	const char *passwd,         /*!< in: password */
	unsigned int master_port,   /*!< in: master port */
	tbr_server_t **tbrm_server, /*!< out: table replication server
				    metadata. */
	size_t     tbrm_rows);      /*!< out: number of rows read */


} // table_replication_metadata

} // mysql

#endif


