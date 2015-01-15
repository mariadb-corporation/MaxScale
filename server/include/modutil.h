#ifndef _MODUTIL_H
#define _MODUTIL_H
/*
 * This file is distributed as part of MaxScale from MariaDB Corporation.  It is free
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
 * Copyright MariaDB Corporation Ab 2014
 */

/**
 * @file modutil.h A set of useful routines for module writers
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 04/06/14	Mark Riddoch		Initial implementation
 * 24/06/14	Mark Riddoch		Add modutil_MySQL_Query to enable multipacket queries
 * 24/10/14	Massimiliano Pinto	Add modutil_send_mysql_err_packet to send a mysql ERR_Packet
 *
 * @endverbatim
 */
#include <buffer.h>
#include <dcb.h>

#define PTR_IS_RESULTSET(b) (b[0] == 0x01 && b[1] == 0x0 && b[2] == 0x0 && b[3] == 0x01)
#define PTR_IS_EOF(b) (b[0] == 0x05 && b[1] == 0x0 && b[2] == 0x0 && b[4] == 0xfe)
#define PTR_IS_OK(b) (b[4] == 0x00)
#define PTR_IS_ERR(b) (b[4] == 0xff)
#define PTR_IS_LOCAL_INFILE(b) (b[4] == 0xfb)

extern int	modutil_is_SQL(GWBUF *);
extern int	modutil_extract_SQL(GWBUF *, char **, int *);
extern int	modutil_MySQL_Query(GWBUF *, char **, int *, int *);
extern char	*modutil_get_SQL(GWBUF *);
extern GWBUF	*modutil_replace_SQL(GWBUF *, char *);
extern char	*modutil_get_query(GWBUF* buf);
extern int	modutil_send_mysql_err_packet(DCB *, int, int, int, const char *, const char *);
GWBUF* 		modutil_get_next_MySQL_packet(GWBUF** p_readbuf);
GWBUF*          modutil_get_complete_packets(GWBUF** p_readbuf);
int 		modutil_MySQL_query_len(GWBUF* buf, int* nbytes_missing);

GWBUF *modutil_create_mysql_err_msg(
	int		packet_number,
	int		affected_rows,
	int		merrno,
	const char	*statemsg,
	const char	*msg);

int modutil_count_signal_packets(GWBUF*,int,int);
#endif
