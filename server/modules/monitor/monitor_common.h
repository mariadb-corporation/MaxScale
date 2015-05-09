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
/**
 * @file monitor_common.h - The generic monitor structures all monitors use
 *
 * Revision History
 *
 * Date      Who             Description
 * 07/05/15  Markus Makela   Initial Implementation
 * @endverbatim
 */

void mon_append_node_names(MONITOR_SERVERS* start,char* str, int len);
char* mon_get_event_type(MONITOR_SERVERS* node);
void monitor_clear_pending_status(MONITOR_SERVERS *ptr, int bit);
void monitor_set_pending_status(MONITOR_SERVERS *ptr, int bit);
bool mon_status_changed(MONITOR_SERVERS* mon_srv);
bool mon_print_fail_status(MONITOR_SERVERS* mon_srv);
#endif
