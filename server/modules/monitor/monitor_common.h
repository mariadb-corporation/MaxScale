#ifndef _MONITOR_COMMON_HG
#define _MONITOR_COMMON_HG
/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2015
 */

#include <server.h>
#include <mysql.h>
#include <monitor.h>
#include <log_manager.h>
#include <externcmd.h>
/**
 * @file monitor_common.h - The generic monitor structures all monitors use
 *
 * Revision History
 *
 * Date      Who             Description
 * 07/05/15  Markus Makela   Initial Implementation
 * @endverbatim
 */

#define MON_ARG_MAX 8192

/** Monitor events that are caused by servers moving from
 * one state to another.*/
typedef enum {
  UNDEFINED_MONITOR_EVENT,
  MASTER_DOWN_EVENT,
  MASTER_UP_EVENT,
  SLAVE_DOWN_EVENT,
  SLAVE_UP_EVENT,
  SERVER_DOWN_EVENT,
  SERVER_UP_EVENT,
  SYNCED_DOWN_EVENT,
  SYNCED_UP_EVENT,
  DONOR_DOWN_EVENT,
  DONOR_UP_EVENT,
  NDB_DOWN_EVENT,
  NDB_UP_EVENT,
  LOST_MASTER_EVENT,
  LOST_SLAVE_EVENT,
  LOST_SYNCED_EVENT,
  LOST_DONOR_EVENT,
  LOST_NDB_EVENT,
  NEW_MASTER_EVENT,
  NEW_SLAVE_EVENT,
  NEW_SYNCED_EVENT,
  NEW_DONOR_EVENT,
  NEW_NDB_EVENT,
  MAX_MONITOR_EVENT
}monitor_event_t;
void mon_append_node_names(MONITOR_SERVERS* start,char* str, int len);
monitor_event_t mon_get_event_type(MONITOR_SERVERS* node);
char* mon_get_event_name(MONITOR_SERVERS* node);
void monitor_clear_pending_status(MONITOR_SERVERS *ptr, int bit);
void monitor_set_pending_status(MONITOR_SERVERS *ptr, int bit);
bool mon_status_changed(MONITOR_SERVERS* mon_srv);
bool mon_print_fail_status(MONITOR_SERVERS* mon_srv);
void monitor_launch_script(MONITOR* mon,MONITOR_SERVERS* ptr, char* script);
int mon_parse_event_string(bool* events, size_t count,char* string);
#endif
