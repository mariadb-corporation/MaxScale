/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2022-01-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>
#include <unistd.h>
#include <binlog_common.h>
#include <blr_constants.h>
#include <maxscale/log.h>

/**
 * @file binlog_common.c - Common binary log code shared between multiple modules
 *
 * This file contains functions that are common to multiple modules that all
 * handle MySQL/MariaDB binlog files.
 */

/**
 * Get the next binlog file name.
 *
 * @param router    The router instance
 * @return 0 on error, >0 as sequence number
 */
int blr_file_get_next_binlogname(const char* binlog_name)
{
    const char* sptr;
    int filenum;

    if ((sptr = strrchr(binlog_name, '.')) == NULL)
    {
        return 0;
    }
    filenum = atoi(sptr + 1);
    if (filenum)
    {
        filenum++;
    }

    return filenum;
}

/**
 * @brief Check if the next binlog file exists and is readable
 * @param binlogdir Directory where the binlogs are
 * @param binlog Current binlog name
 * @return True if next binlog file exists and is readable
 */
bool binlog_next_file_exists(const char* binlogdir, const char* binlog)
{
    bool rval = false;
    int filenum = blr_file_get_next_binlogname(binlog);

    if (filenum)
    {
        const char* sptr = strrchr(binlog, '.');

        if (sptr)
        {
            char buf[BLRM_BINLOG_NAME_STR_LEN + 1];
            char filename[PATH_MAX + 1];
            char next_file[BLRM_BINLOG_NAME_STR_LEN + 1 + 20];
            int offset = sptr - binlog;
            memcpy(buf, binlog, offset);
            buf[offset] = '\0';
            snprintf(next_file, sizeof(next_file), BINLOG_NAMEFMT, buf, filenum);
            snprintf(filename, PATH_MAX, "%s/%s", binlogdir, next_file);
            filename[PATH_MAX] = '\0';

            /* Next file in sequence doesn't exist */
            if (access(filename, R_OK) == -1)
            {
                MXS_DEBUG("File '%s' does not yet exist.", filename);
            }
            else
            {
                rval = true;
            }
        }
    }

    return rval;
}

/**
 * Extract a numeric field from a packet of the specified number of bits
 *
 * @param src   The raw packet source
 * @param birs  The number of bits to extract (multiple of 8)
 */
uint32_t extract_field(uint8_t* src, int bits)
{
    uint32_t rval = 0, shift = 0;

    while (bits > 0)
    {
        rval |= (*src++) << shift;
        shift += 8;
        bits -= 8;
    }
    return rval;
}

/**
 * Convert binlog event type to string
 * @param type Event type
 * @return Event type in string format
 */
const char* binlog_event_name(int type)
{
    switch (type)
    {
    case START_EVENT_V3:
        return "START_EVENT_V3";

    case QUERY_EVENT:
        return "QUERY_EVENT";

    case STOP_EVENT:
        return "STOP_EVENT";

    case ROTATE_EVENT:
        return "ROTATE_EVENT";

    case INTVAR_EVENT:
        return "INTVAR_EVENT";

    case LOAD_EVENT:
        return "LOAD_EVENT";

    case SLAVE_EVENT:
        return "SLAVE_EVENT";

    case CREATE_FILE_EVENT:
        return "CREATE_FILE_EVENT";

    case APPEND_BLOCK_EVENT:
        return "APPEND_BLOCK_EVENT";

    case EXEC_LOAD_EVENT:
        return "EXEC_LOAD_EVENT";

    case DELETE_FILE_EVENT:
        return "DELETE_FILE_EVENT";

    case NEW_LOAD_EVENT:
        return "NEW_LOAD_EVENT";

    case RAND_EVENT:
        return "RAND_EVENT";

    case USER_VAR_EVENT:
        return "USER_VAR_EVENT";

    case FORMAT_DESCRIPTION_EVENT:
        return "FORMAT_DESCRIPTION_EVENT";

    case XID_EVENT:
        return "XID_EVENT";

    case BEGIN_LOAD_QUERY_EVENT:
        return "BEGIN_LOAD_QUERY_EVENT";

    case EXECUTE_LOAD_QUERY_EVENT:
        return "EXECUTE_LOAD_QUERY_EVENT";

    case TABLE_MAP_EVENT:
        return "TABLE_MAP_EVENT";

    case WRITE_ROWS_EVENTv0:
        return "WRITE_ROWS_EVENTv0";

    case UPDATE_ROWS_EVENTv0:
        return "UPDATE_ROWS_EVENTv0";

    case DELETE_ROWS_EVENTv0:
        return "DELETE_ROWS_EVENTv0";

    case WRITE_ROWS_EVENTv1:
        return "WRITE_ROWS_EVENTv1";

    case UPDATE_ROWS_EVENTv1:
        return "UPDATE_ROWS_EVENTv1";

    case DELETE_ROWS_EVENTv1:
        return "DELETE_ROWS_EVENTv1";

    case INCIDENT_EVENT:
        return "INCIDENT_EVENT";

    case HEARTBEAT_EVENT:
        return "HEARTBEAT_EVENT";

    case IGNORABLE_EVENT:
        return "IGNORABLE_EVENT";

    case ROWS_QUERY_EVENT:
        return "ROWS_QUERY_EVENT";

    case WRITE_ROWS_EVENTv2:
        return "WRITE_ROWS_EVENTv2";

    case UPDATE_ROWS_EVENTv2:
        return "UPDATE_ROWS_EVENTv2";

    case DELETE_ROWS_EVENTv2:
        return "DELETE_ROWS_EVENTv2";

    case GTID_EVENT:
        return "GTID_EVENT";

    case ANONYMOUS_GTID_EVENT:
        return "ANONYMOUS_GTID_EVENT";

    case PREVIOUS_GTIDS_EVENT:
        return "PREVIOUS_GTIDS_EVENT";

    case MARIADB_ANNOTATE_ROWS_EVENT:
        return "MARIADB_ANNOTATE_ROWS_EVENT";

    case MARIADB10_BINLOG_CHECKPOINT_EVENT:
        return "MARIADB10_BINLOG_CHECKPOINT_EVENT";

    case MARIADB10_GTID_EVENT:
        return "MARIADB10_GTID_EVENT";

    case MARIADB10_GTID_GTID_LIST_EVENT:
        return "MARIADB10_GTID_GTID_LIST_EVENT";

    default:
        return "UNKNOWN_EVENT";
    }
}
