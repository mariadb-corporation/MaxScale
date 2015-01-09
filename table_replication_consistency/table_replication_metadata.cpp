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
Created: 15-07-2013
Updated:
*/
#include "binlog_api.h"
#include <getopt.h>
#include <iostream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <regex.h>
#include <algorithm>
#include "listener_exception.h"
#include <mysql.h>
#include <mysqld_error.h>
#include "table_replication_metadata.h"
#include "table_replication_consistency.h"
#include "log_manager.h"

namespace mysql {

namespace table_replication_metadata {

/***********************************************************************//**
Internal function to write error messages to the log file.
*/
static void
tbrm_report_error(
/*==============*/
	MYSQL *con,          /*!< in: MySQL connection */
        const char *message, /*!< in: Error message */
        const char *file,    /*!< in: File name */
        int line)            /*!< in: Line number */
{
	skygw_log_write_flush( LOGFILE_ERROR,
		(char *)"%s at file %s line %d", message, file, line);
	if (con != NULL) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"%s", mysql_error(con));
		mysql_close(con);
	}
}

/***********************************************************************//**
Internal function to write statement error messages to the log file.
*/
static void
tbrm_stmt_error(
/*============*/
	MYSQL_STMT *stmt,    /*!< in: MySQL statement */
	const char *message, /*!< in: Error message */
	const char *file,    /*!< in: File name */
	int line)            /*!< in: Line number */
{
	skygw_log_write_flush( LOGFILE_ERROR,
		(char *)"%s at file %s line %d", message, file, line);

	if (stmt != NULL)
	{
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Error %u (%s): %s\n",
			mysql_stmt_errno (stmt),
			mysql_stmt_sqlstate (stmt),
			mysql_stmt_error (stmt));
	}
}

/***********************************************************************//**
Inspect master data dictionary and if necessary table replication
consistency metadata is not created, create it.
@return false if create failed, true if metadata already created or
create succeeded */
static bool
tbrm_create_metadata(
/*=================*/
	const char *master_host,  /*!< in: Master host name */
	const char *user,         /*!< in: Username */
	const char *passwd,       /*!< in: Passwd */
	unsigned int master_port) /*!< in: Master port */
{
	MYSQL *con = mysql_init(NULL);
	unsigned int myerrno=0;

	if (!con) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Mysql init failed", mysql_error(con));
		return false;
	}

	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, "libmysqld_client");
	mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL);

	if (!mysql_real_connect(con, master_host, user, passwd, NULL, master_port, NULL, 0)) {
		tbrm_report_error(con, "Error: mysql_real_connect failed", __FILE__, __LINE__);
		goto error_exit;
	}

	// Check is the database there
	mysql_query(con, "USE SKYSQL_GATEWAY_METADATA");
	myerrno = mysql_errno(con);

	if (myerrno == 0) {
		// Database found, assuming everyting ok
		return true;
	} else if (myerrno != ER_BAD_DB_ERROR) {
		tbrm_report_error(con, "Error: mysql_query(USE_SKYSQL_GATEWAY_METADATA) failed", __FILE__, __LINE__);
		goto error_exit;
	}

	// Create databse
	mysql_query(con, "CREATE DATABASE SKYSQL_GATEWAY_METADATA");

	if (mysql_errno(con) != 0) {
		tbrm_report_error(con, "mysql_query(CREATE DATABASE SKYSQL_GATEWAY_METADATA) failed", __FILE__, __LINE__);
		goto error_exit;
	}

	// Set correct database
	mysql_query(con, "USE SKYSQL_GATEWAY_METADATA");

	if (mysql_errno(con) != 0) {
		tbrm_report_error(con, "Error: mysql_query(USE_SKYSQL_GATEWAY_METADATA) failed", __FILE__, __LINE__);
		goto error_exit;
	}

	// Create consistency table
	mysql_query(con, "CREATE TABLE TABLE_REPLICATION_CONSISTENCY("
		"DB_TABLE_NAME VARCHAR(255) NOT NULL,"
		"SERVER_ID INT NOT NULL,"
		"GTID VARBINARY(255),"
		"BINLOG_POS BIGINT NOT NULL,"
		"GTID_KNOWN INT,"
		"PRIMARY KEY(DB_TABLE_NAME, SERVER_ID)) ENGINE=InnoDB");

	if (mysql_errno(con) != 0) {
		tbrm_report_error(con, "Error: Create table failed", __FILE__, __LINE__);
		goto error_exit;
	}

	// Above clauses not really transactional, but lets play safe
	mysql_query(con, "COMMIT");

	if (mysql_errno(con) != 0) {
		tbrm_report_error(con, "Error: Commit failed", __FILE__, __LINE__);
		goto error_exit;
	}

	// Create servers table
	mysql_query(con, "CREATE TABLE TABLE_REPLICATION_SERVERS("
		"SERVER_ID INT NOT NULL,"
		"BINLOG_POS BIGINT NOT NULL,"
		"GTID VARBINARY(255),"
		"GTID_KNOWN INT,"
		"SERVER_TYPE INT,"
		"PRIMARY KEY(SERVER_ID)) ENGINE=InnoDB");

	if (mysql_errno(con) != 0) {
		tbrm_report_error(con, "Error: Create table failed", __FILE__, __LINE__);
		goto error_exit;
	}

	// Above clauses not really transactional, but lets play safe
	mysql_query(con, "COMMIT");

	if (mysql_errno(con) != 0) {
		tbrm_report_error(con, "Error: Commit failed", __FILE__, __LINE__);
		goto error_exit;
	}

	mysql_close(con);

	// Done
	return true;

error_exit:

	if (con) {
		mysql_close(con);
	}

	return false;
}

/***********************************************************************//**
Read table replication consistency metadata from the MySQL master server.
This function will create necessary database and table if they are not
yet created.
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
	size_t *tbrm_rows)          /*!< out: number of rows read */
{
	unsigned int myerrno=0;
	boost::uint64_t nrows=0;
	boost::uint64_t i=0;
	MYSQL_RES *result = NULL;
	tbr_metadata_t *tm=NULL;

	tbrm_create_metadata(master_host, user, passwd, master_port);

	MYSQL *con = mysql_init(NULL);

	if (!con) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Error: MySQL init failed");
		return false;
	}

	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, "libmysqld_client");
	mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL);

	if (!mysql_real_connect(con, master_host, user, passwd, NULL, master_port, NULL, 0)) {
		tbrm_report_error(con, "Error: mysql_real_connect failed", __FILE__, __LINE__);
		goto error_exit;
	}

	mysql_query(con, "USE SKYSQL_GATEWAY_METADATA");
	myerrno = mysql_errno(con);

	if (myerrno != 0) {
		tbrm_report_error(con, "Error: Database set failed", __FILE__, __LINE__);
		goto error_exit;
	}

	mysql_query(con, "SELECT * FROM TABLE_REPLICATION_CONSISTENCY");
	myerrno = mysql_errno(con);

	if (myerrno != 0) {
		tbrm_report_error(con,"Error: Select from table_replication_consistency failed", __FILE__, __LINE__);
		goto error_exit;
	}

	result = mysql_store_result(con);

	if (!result) {
		tbrm_report_error(con, "Error: mysql_store_result failed", __FILE__, __LINE__);
		goto error_exit;
	}

	nrows = mysql_num_rows(result);

	tm = (tbr_metadata_t*) malloc(nrows * sizeof(tbr_metadata_t));

	if (!tm) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Error: Out of memory");
		goto error_exit;
	}

	memset(tm, 0, nrows * sizeof(tbr_metadata_t));
	*tbrm_rows = nrows;
	*tbrm_meta = tm;

	for(i=0;i < nrows; i++) {
		MYSQL_ROW row = mysql_fetch_row(result);
		unsigned long *lengths = mysql_fetch_lengths(result);
		// DB_TABLE_NAME
		tm[i].db_table = (unsigned char *)malloc(lengths[0]);

		if (!tm[i].db_table) {
			skygw_log_write_flush( LOGFILE_ERROR,
				(char *)"Error: Out of memory");
			goto error_exit;
		}

		strcpy((char *)tm[i].db_table, row[0]);
		// SERVER_ID
		tm[i].server_id = atol(row[1]);
		// GTID
		tm[i].gtid = (unsigned char *)malloc((lengths[2])*sizeof(unsigned char));

		if (!tm[i].gtid) {
			free(tm[i].db_table);
			skygw_log_write_flush( LOGFILE_ERROR,
				(char *)"Error: Out of memory");
			goto error_exit;
		}

		memcpy(tm[i].gtid, row[2], lengths[2]);
		tm[i].gtid_len = lengths[2];
		// BINLOG_POS
		tm[i].binlog_pos = atoll(row[3]);
		// GTID_KNOWN
		tm[i].gtid_known = atol(row[4]);
	}

	mysql_free_result(result);
	mysql_close(con);

	return true;

 error_exit:

	if (tm) {
		for(size_t k=0;i < i; k++) {
			free(tm[k].db_table);
			free(tm[k].gtid);
		}
		free(tm);
		*tbrm_rows = 0;
		*tbrm_meta = NULL;
	}

	if (result) {
		mysql_free_result(result);
	}

	if (con) {
		mysql_close(con);
	}

	return false;
}

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
	size_t tbrm_rows)           /*!< in: number of rows read */
{
        int myerrno=0;
	boost::uint32_t i;
	MYSQL_STMT *sstmt=NULL;
	MYSQL_STMT *istmt=NULL;
	MYSQL_STMT *ustmt=NULL;
	MYSQL_BIND sparam[2];
	MYSQL_BIND iparam[5];
	MYSQL_BIND uparam[5];
	MYSQL_BIND result[1];
	char *dbtable=NULL;
	void *gtid=NULL;
	int gtidknown;
	int serverid;
	boost::uint64_t binlogpos;

	// Query to find out if the row already exists on table
	const char *sst = "SELECT BINLOG_POS FROM TABLE_REPLICATION_CONSISTENCY WHERE"
		" DB_TABLE_NAME=? and SERVER_ID=?";

	// Insert Query
	const char *ist = "INSERT INTO TABLE_REPLICATION_CONSISTENCY(DB_TABLE_NAME,"
		" SERVER_ID, GTID, BINLOG_POS, GTID_KNOWN) VALUES"
		"(?, ?, ?, ?, ?)";

	// Update Query
	const char *ust = "UPDATE TABLE_REPLICATION_CONSISTENCY "
		"SET GTID=?, BINLOG_POS=?, GTID_KNOWN=?"
		" WHERE DB_TABLE_NAME=? AND SERVER_ID=?";

	MYSQL *con = mysql_init(NULL);

	if (!con) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Mysql init failed", mysql_error(con));
		return false;
	}

	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, "libmysqld_client");
	mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL);

	if (!mysql_real_connect(con, master_host, user, passwd, NULL, master_port, NULL, 0)) {
		tbrm_report_error(con, "Error: mysql_real_connect failed", __FILE__, __LINE__);
		goto error_exit;
	}

	mysql_query(con, "USE SKYSQL_GATEWAY_METADATA");
	myerrno = mysql_errno(con);

	if (myerrno != 0) {
		tbrm_report_error(con, "Error: Database set failed", __FILE__, __LINE__);
	}

	// Allocate statement handlers
	sstmt = mysql_stmt_init(con);
	istmt = mysql_stmt_init(con);
	ustmt = mysql_stmt_init(con);

	if (sstmt == NULL || istmt == NULL || ustmt == NULL) {
		tbrm_report_error(con, "Could not initialize statement handler", __FILE__, __LINE__);
		goto error_exit;
	}

	// Prepare the statements
	if (mysql_stmt_prepare(sstmt, sst, strlen(sst)) != 0) {
		tbrm_stmt_error(sstmt, "Error: Could not prepare select statement", __FILE__, __LINE__);
		goto error_exit;
	}
	if (mysql_stmt_prepare(istmt, ist, strlen(ist)) != 0) {
		tbrm_stmt_error(istmt, "Error: Could not prepare insert statement", __FILE__, __LINE__);
		goto error_exit;
	}
	if (mysql_stmt_prepare(ustmt, ust, strlen(ust)) != 0) {
		tbrm_stmt_error(ustmt, "Error: Could not prepare update statement", __FILE__, __LINE__);
		goto error_exit;
	}

	// Initialize the parameters
	memset (sparam, 0, sizeof (sparam));
	memset (iparam, 0, sizeof (iparam));
	memset (uparam, 0, sizeof (uparam));
	memset (result, 0, sizeof (result));

	// Init param structure
	// Select
	sparam[0].buffer_type     = MYSQL_TYPE_VARCHAR;
	sparam[1].buffer_type     = MYSQL_TYPE_LONG;
	sparam[1].buffer         = (void *) &serverid;
	// Insert
	iparam[0].buffer_type     = MYSQL_TYPE_VARCHAR;
	iparam[1].buffer_type     = MYSQL_TYPE_LONG;
	iparam[1].buffer         = (void *) &serverid;
	iparam[2].buffer_type     = MYSQL_TYPE_BLOB;
	iparam[3].buffer_type     = MYSQL_TYPE_LONGLONG;
	iparam[3].buffer         = (void *) &binlogpos;
	iparam[4].buffer_type     = MYSQL_TYPE_SHORT;
	iparam[4].buffer         = (void *) &gtidknown;
	// Update
	uparam[0].buffer_type     = MYSQL_TYPE_BLOB;
	uparam[1].buffer_type     = MYSQL_TYPE_LONGLONG;
	uparam[1].buffer         = (void *) &binlogpos;
	uparam[2].buffer_type     = MYSQL_TYPE_SHORT;
	uparam[2].buffer         = (void *) &gtidknown;
	uparam[3].buffer_type     = MYSQL_TYPE_VARCHAR;
	uparam[4].buffer_type     = MYSQL_TYPE_LONG;
	uparam[4].buffer         = (void *) &serverid;
	// Result set for select
	result[0].buffer_type     = MYSQL_TYPE_LONGLONG;
	result[0].buffer          = &binlogpos;


	// Iterate through the data
	for(i = 0; i < tbrm_rows; i++) {
		// Start from Select, we need to know if the consistency
		// information for this table, server pair is already
		// in metadata or not.

		dbtable = (char *)tbrm_meta[i]->db_table;
		gtid    = (char *)tbrm_meta[i]->gtid;
		gtidknown = tbrm_meta[i]->gtid_known;
		serverid  = tbrm_meta[i]->server_id;
		uparam[3].buffer         = (void *) dbtable;

		sparam[0].buffer         = (void *) dbtable;
		uparam[0].buffer         = (void *) gtid;
		iparam[0].buffer         = (void *) dbtable;
		iparam[2].buffer         = (void *) gtid;
		sparam[0].buffer_length = strlen(dbtable);
		uparam[3].buffer_length = sparam[0].buffer_length;
		iparam[0].buffer_length = sparam[0].buffer_length;
		uparam[0].buffer_length = tbrm_meta[i]->gtid_len;
		iparam[2].buffer_length = tbrm_meta[i]->gtid_len;

		// Bind param structure to statement
		if (mysql_stmt_bind_param(sstmt, sparam) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not bind select parameters", __FILE__, __LINE__);
			goto error_exit;
		}

		// Bind result structure to statement
		if (mysql_stmt_bind_result(sstmt, result) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not bind select return parameters", __FILE__, __LINE__);
			goto error_exit;
		}

		// Execute!!
		if (mysql_stmt_execute(sstmt) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not execute select statement", __FILE__, __LINE__);
			goto error_exit;
		}

		// Store result
		if (mysql_stmt_store_result(sstmt) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not buffer result set", __FILE__, __LINE__);
			goto error_exit;
		}

		// Fetch result
		myerrno = mysql_stmt_fetch(sstmt);
		if (myerrno != 0 && myerrno != MYSQL_NO_DATA) {
			tbrm_stmt_error(sstmt, "Error: Could not fetch result set", __FILE__, __LINE__);
			goto error_exit;
		}

		// If fetch returned 0 rows, it means that this table, serverid
		// pair was found from metadata, we might need to update
		// the consistency information.
		if (myerrno == 0) {
			// We update the consistency if and only if the
			// binlog position for this table has changed
			if (binlogpos != tbrm_meta[i]->binlog_pos) {
				// Update the consistency information
				binlogpos = tbrm_meta[i]->binlog_pos;

				// Bind param structure to statement
				if (mysql_stmt_bind_param(ustmt, uparam) != 0) {
					tbrm_stmt_error(ustmt, "Error: Could not bind update parameters", __FILE__, __LINE__);
					goto error_exit;
				}
				// Execute!!
				if (mysql_stmt_execute(ustmt) != 0) {
					tbrm_stmt_error(ustmt, "Error: Could not execute update statement", __FILE__, __LINE__);
					goto error_exit;
				}
				if (tbr_debug) {
					skygw_log_write_flush( LOGFILE_TRACE,
						(char *)"TRC Debug: Metadata state updated for %s in server %d is binlog_pos %lu gtid '%s'",
						dbtable, serverid, binlogpos, gtid);
				}
			}

		} else {
			// Insert the consistency information
			binlogpos = tbrm_meta[i]->binlog_pos;
			// Bind param structure to statement
			if (mysql_stmt_bind_param(istmt, iparam) != 0) {
				tbrm_stmt_error(istmt, "Error: Could not bind insert parameters", __FILE__, __LINE__);
				goto error_exit;
			}

			// Execute!!
			if (mysql_stmt_execute(istmt) != 0) {
				tbrm_stmt_error(istmt, "Error: Could not execute insert statement", __FILE__, __LINE__);
				goto error_exit;
			}

			if (tbr_debug) {
				skygw_log_write_flush( LOGFILE_TRACE,
					(char *)"TRC Debug: Metadata state inserted for %s in server %d is binlog_pos %lu gtid '%s'",
					dbtable, serverid, binlogpos, gtid);
			}
		}
	}

	return true;

 error_exit:
	// Cleanup
	if (sstmt) {
		if (mysql_stmt_close(sstmt)) {
			tbrm_stmt_error(sstmt, "Error: Could not close select statement", __FILE__, __LINE__);
		}
	}

	if (istmt) {
		if (mysql_stmt_close(istmt)) {
			tbrm_stmt_error(istmt, "Error: Could not close select statement", __FILE__, __LINE__);
		}
	}

	if (ustmt) {
		if (mysql_stmt_close(ustmt)) {
			tbrm_stmt_error(ustmt, "Error: Could not close select statement", __FILE__, __LINE__);
		}
	}

	if (con) {
		mysql_close(con);
	}

	return false;

}

/***********************************************************************//**
Read table replication server metadata from the MySQL master server.
This function will create necessary database and table if they are not
yet created.
@return false if read failed, true if read succeeded */
bool
tbrm_read_server_metadata(
/*======================*/
	const char *master_host,    /*!< in: Master hostname */
	const char *user,           /*!< in: username */
	const char *passwd,         /*!< in: password */
	unsigned int master_port,   /*!< in: master port */
	tbr_server_t **tbrm_servers,/*!< out: table replication server
				    metadata. */
	size_t *tbrm_rows)          /*!< out: number of rows read */
{
	unsigned int myerrno=0;
	boost::uint64_t nrows=0;
	boost::uint64_t i=0;
	MYSQL_RES *result = NULL;
	tbr_server_t *ts=NULL;

	tbrm_create_metadata(master_host, user, passwd, master_port);

	MYSQL *con = mysql_init(NULL);

	if (!con) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Mysql init failed", mysql_error(con));

		return false;
	}

	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, "libmysqld_client");
	mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL);

	if (!mysql_real_connect(con, master_host, user, passwd, NULL, master_port, NULL, 0)) {
		tbrm_report_error(con, "Error: mysql_real_connect failed", __FILE__, __LINE__);
		goto error_exit;
	}

	mysql_query(con, "USE SKYSQL_GATEWAY_METADATA");
	myerrno = mysql_errno(con);

	if (myerrno != 0) {
		tbrm_report_error(con, "Error: Database set failed", __FILE__, __LINE__);
		goto error_exit;
	}

	mysql_query(con, "SELECT * FROM TABLE_REPLICATION_SERVERS");
	myerrno = mysql_errno(con);

	if (myerrno != 0) {
		tbrm_report_error(con,"Error: Select from table_replication_consistency failed", __FILE__, __LINE__);
		goto error_exit;
	}

	result = mysql_store_result(con);

	if (!result) {
		tbrm_report_error(con, "Error: mysql_store_result failed", __FILE__, __LINE__);
		goto error_exit;
	}

	nrows = mysql_num_rows(result);

	ts = (tbr_server_t*) malloc(nrows * sizeof(tbr_server_t));

	if(!ts) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Error: Out of memory");
		goto error_exit;
	}

	*tbrm_rows = nrows;
	*tbrm_servers = ts;

	for(i=0;i < nrows; i++) {
		MYSQL_ROW row = mysql_fetch_row(result);
		unsigned long *lengths = mysql_fetch_lengths(result);
		// SERVER_ID
		ts[i].server_id = atol(row[0]);
		// BINLOG_POS
		ts[i].binlog_pos = atoll(row[1]);
		// GTID
		ts[i].gtid = (unsigned char *)malloc((lengths[2])*sizeof(unsigned char));

		if (!ts[i].gtid) {
			skygw_log_write_flush( LOGFILE_ERROR,
				(char *)"Error: Out of memory");
			goto error_exit;
		}

		memcpy(ts[i].gtid, row[2], lengths[2]);
		ts[i].gtid_len = lengths[2];
		// GTID_KNOWN
		ts[i].gtid_known = atol(row[3]);
		// SERVER_TYPE
		ts[i].server_type = atol(row[4]);
	}

	mysql_free_result(result);
	mysql_close(con);

	return true;

 error_exit:
	if (ts) {
		for(size_t k=0;i < i; k++) {
			free(ts[k].gtid);
		}
		free(ts);
		*tbrm_rows = 0;
		*tbrm_servers = NULL;
	}

	if (result) {
		mysql_free_result(result);
	}

	if (con) {
		mysql_close(con);
	}

	return false;
}

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
	tbr_server_t **tbrm_servers,/*!< in: table replication server
				    metadata. */
	size_t tbrm_rows)           /*!< in: number of rows read */
{
        int myerrno=0;
	boost::uint32_t i;
	MYSQL_STMT *sstmt=NULL;
	MYSQL_STMT *istmt=NULL;
	MYSQL_STMT *ustmt=NULL;
	MYSQL_BIND sparam[1];
	MYSQL_BIND iparam[5];
	MYSQL_BIND uparam[4];
	MYSQL_BIND result[1];
	char *dbtable;
	void *gtid;
	int gtidknown;
	unsigned int serverid;
	int servertype;
	boost::uint64_t binlogpos;

	// Query to find out if the row already exists on table
	const char *sst = "SELECT BINLOG_POS FROM TABLE_REPLICATION_CONSISTENCY WHERE"
		" SERVER_ID=?";

	// Insert Query
	const char *ist = "INSERT INTO TABLE_REPLICATION_SERVERS("
		" SERVER_ID, GTID, BINLOG_POS, GTID_KNOWN, SERVER_TYPE) VALUES"
		"(?, ?, ?, ?, ?)";

	// Update Query
	const char *ust = "UPDATE TABLE_REPLICATION_SERVERS "
		"SET GTID=?, BINLOG_POS=?, GTID_KNOWN=?"
		" WHERE SERVER_ID=?";

	MYSQL *con = mysql_init(NULL);

	if (!con) {
		skygw_log_write_flush( LOGFILE_ERROR,
			(char *)"Mysql init failed", mysql_error(con));
		return false;
	}

	mysql_options(con, MYSQL_READ_DEFAULT_GROUP, "libmysqld_client");
	mysql_options(con, MYSQL_OPT_USE_REMOTE_CONNECTION, NULL);

	if (!mysql_real_connect(con, master_host, user, passwd, NULL, master_port, NULL, 0)) {
		tbrm_report_error(con, "Error: mysql_real_connect failed", __FILE__, __LINE__);
		goto error_exit;
	}

	mysql_query(con, "USE SKYSQL_GATEWAY_METADATA");
	myerrno = mysql_errno(con);

	if (myerrno != 0) {
		tbrm_report_error(con, "Error: Database set failed", __FILE__, __LINE__);
	}

	// Allocate statement handlers
	sstmt = mysql_stmt_init(con);
	istmt = mysql_stmt_init(con);
	ustmt = mysql_stmt_init(con);

	if (sstmt == NULL || istmt == NULL || ustmt == NULL) {
		tbrm_report_error(con, "Could not initialize statement handler", __FILE__, __LINE__);
		goto error_exit;
	}

	// Prepare the statements
	if (mysql_stmt_prepare(sstmt, sst, strlen(sst)) != 0) {
		tbrm_stmt_error(sstmt, "Error: Could not prepare select statement", __FILE__, __LINE__);
		goto error_exit;
	}
	if (mysql_stmt_prepare(istmt, ist, strlen(ist)) != 0) {
		tbrm_stmt_error(istmt, "Error: Could not prepare insert statement", __FILE__, __LINE__);
		goto error_exit;
	}
	if (mysql_stmt_prepare(ustmt, ust, strlen(ust)) != 0) {
		tbrm_stmt_error(ustmt, "Error: Could not prepare update statement", __FILE__, __LINE__);
		goto error_exit;
	}

	// Initialize the parameters
	memset (sparam, 0, sizeof (sparam));
	memset (iparam, 0, sizeof (iparam));
	memset (uparam, 0, sizeof (uparam));
	memset (result, 0, sizeof (result));

	// Init param structure
	// Select
	sparam[0].buffer_type     = MYSQL_TYPE_LONG;
	sparam[0].buffer         = (void *) &serverid;
	// Insert
	iparam[0].buffer_type     = MYSQL_TYPE_LONG;
	iparam[0].buffer         = (void *) &serverid;
	iparam[1].buffer_type     = MYSQL_TYPE_BLOB;
	iparam[2].buffer_type     = MYSQL_TYPE_LONGLONG;
	iparam[2].buffer         = (void *) &binlogpos;
	iparam[3].buffer_type     = MYSQL_TYPE_SHORT;
	iparam[3].buffer         = (void *) &gtidknown;
	iparam[4].buffer_type     = MYSQL_TYPE_LONG;
	iparam[4].buffer         = (void *) &servertype;
	// Update
	uparam[0].buffer_type     = MYSQL_TYPE_BLOB;
	uparam[1].buffer_type     = MYSQL_TYPE_LONGLONG;
	uparam[1].buffer         = (void *) &binlogpos;
	uparam[2].buffer_type     = MYSQL_TYPE_SHORT;
	uparam[2].buffer         = (void *) &gtidknown;
	uparam[3].buffer_type     = MYSQL_TYPE_LONG;
	uparam[3].buffer         = (void *) &serverid;
	// Result set for select
	result[0].buffer_type     = MYSQL_TYPE_LONGLONG;
	result[0].buffer          = &binlogpos;


	// Iterate through the data
	for(i = 0; i < tbrm_rows; i++) {
		// Start from Select, we need to know if the consistency
		// information for this table, server pair is already
		// in metadata or not.

		gtid    = (char *)tbrm_servers[i]->gtid;
		gtidknown = tbrm_servers[i]->gtid_known;
		serverid  = tbrm_servers[i]->server_id;
		servertype = tbrm_servers[i]->server_type;

		iparam[1].buffer         = (void *) gtid;
		uparam[0].buffer         = (void *) gtid;
		uparam[0].buffer_length = tbrm_servers[i]->gtid_len;
		iparam[1].buffer_length = tbrm_servers[i]->gtid_len;

		// Bind param structure to statement
		if (mysql_stmt_bind_param(sstmt, sparam) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not bind select parameters", __FILE__, __LINE__);
			goto error_exit;
		}

		// Bind result structure to statement
		if (mysql_stmt_bind_result(sstmt, result) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not bind select return parameters", __FILE__, __LINE__);
			goto error_exit;
		}

		// Execute!!
		if (mysql_stmt_execute(sstmt) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not execute select statement", __FILE__, __LINE__);
			goto error_exit;
		}

		// Store result
		if (mysql_stmt_store_result(sstmt) != 0) {
			tbrm_stmt_error(sstmt, "Error: Could not buffer result set", __FILE__, __LINE__);
			goto error_exit;
		}

		// Fetch result
		myerrno = mysql_stmt_fetch(sstmt);
		if (myerrno != 0 && myerrno !=  MYSQL_NO_DATA) {
			tbrm_stmt_error(sstmt, "Error: Could not fetch result set", __FILE__, __LINE__);
			goto error_exit;
		}

		// If fetch returned 0 rows, it means that this table, serverid
		// pair was found from metadata, we might need to update
		// the consistency information.
		if (myerrno == 0) {
			// We update the consistency if and only if the
			// binlog position for this table has changed
			if (binlogpos != tbrm_servers[i]->binlog_pos) {
				// Update the consistency information
				binlogpos = tbrm_servers[i]->binlog_pos;

				// Bind param structure to statement
				if (mysql_stmt_bind_param(ustmt, uparam) != 0) {
					tbrm_stmt_error(ustmt, "Error: Could not bind update parameters", __FILE__, __LINE__);
					goto error_exit;
				}
				// Execute!!
				if (mysql_stmt_execute(ustmt) != 0) {
					tbrm_stmt_error(ustmt, "Error: Could not execute update statement", __FILE__, __LINE__);
					goto error_exit;
				}
				if (tbr_debug) {
					skygw_log_write_flush( LOGFILE_TRACE,
						(char *)"TRC Debug: Metadata state updated for %s in server %d is binlog_pos %lu gtid '%s'",
						dbtable, serverid, binlogpos, gtid);
				}
			}

		} else {
			// Insert the consistency information
			binlogpos = tbrm_servers[i]->binlog_pos;
			// Bind param structure to statement
			if (mysql_stmt_bind_param(istmt, iparam) != 0) {
				tbrm_stmt_error(istmt, "Error: Could not bind insert parameters", __FILE__, __LINE__);
				goto error_exit;
			}

			// Execute!!
			if (mysql_stmt_execute(istmt) != 0) {
				tbrm_stmt_error(istmt, "Error: Could not execute insert statement", __FILE__, __LINE__);
				goto error_exit;
			}

			if (tbr_debug) {
				skygw_log_write_flush( LOGFILE_TRACE,
					(char *)"TRC Debug: Metadata state inserted for %s in server %d is binlog_pos %lu gtid '%s'",
					dbtable, serverid, binlogpos, gtid);
			}
		}
	}

	return true;

 error_exit:
	// Cleanup
	if (sstmt) {
		if (mysql_stmt_close(sstmt)) {
			tbrm_stmt_error(sstmt, "Error: Could not close select statement", __FILE__, __LINE__);
		}
	}

	if (istmt) {
		if (mysql_stmt_close(istmt)) {
			tbrm_stmt_error(istmt, "Error: Could not close select statement", __FILE__, __LINE__);
		}
	}

	if (ustmt) {
		if (mysql_stmt_close(ustmt)) {
			tbrm_stmt_error(ustmt, "Error: Could not close select statement", __FILE__, __LINE__);
		}
	}

	if (con) {
		mysql_close(con);
	}

	return false;

}



} // table_replication_metadata

} // mysql
