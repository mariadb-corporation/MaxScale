/*
 * This file is distributed as part of MaxScale from SkySQL.  It is free
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
 * Copyright SkySQL Ab 2014
 */

/**
 * @file modutil.c  - Implementation of useful routines for modules
 *
 * @verbatim
 * Revision History
 *
 * Date		Who		Description
 * 04/06/14	Mark Riddoch	Initial implementation
 *
 * @endverbatim
 */
#include <buffer.h>

/**
 * Check if a GWBUF structure is a MySQL COM_QUERY packet
 *
 * @param	buf	Buffer to check
 * @return	True if GWBUF is a COM_QUERY packet
 */
int
modutil_is_SQL(GWBUF *buf)
{
unsigned char	*ptr;

	if (GWBUF_LENGTH(buf) < 5)
		return 0;
	ptr = GWBUF_DATA(buf);
	return ptr[4] == 0x03;		// COM_QUERY
}

/**
 * Extract the SQL portion of a COM_QUERY packet
 *
 * NB This sets *sql to point into the packet and does not
 * allocate any new storage. The string pointed to by *sql is
 * not NULL terminated.
 *
 * This routine is very simplistic and does not deal with SQL text
 * that spans multiple buffers.
 *
 * @param	buf	The packet buffer
 * @param	sql	Pointer that is set to point at the SQL data
 * @param	length	Length of the SQL data
 * @return	True if the packet is a COM_QUERY packet
 */
int
modutil_extract_SQL(GWBUF *buf, char **sql, int *length)
{
char	*ptr;

	if (!modutil_is_SQL(buf))
		return 0;
	ptr = GWBUF_DATA(buf);
	*length = *ptr++;
	*length += (*ptr++ << 8);
	*length += (*ptr++ << 8);
        ptr += 2;  // Skip sequence id	and COM_QUERY byte
	*length = *length - 1;
	*sql = ptr;
}


GWBUF *
modutil_replace_SQL(GWBUF *orig, char *sql)
{
char	*ptr;
int	length, newlength;
GWBUF	*addition;

	if (!modutil_is_SQL(orig))
		return 0;
	ptr = GWBUF_DATA(orig);
	length = *ptr++;
	length += (*ptr++ << 8);
	length += (*ptr++ << 8);
        ptr += 2;  // Skip sequence id	and COM_QUERY byte

	newlength = strlen(sql);
	if (length - 1 == newlength)
	{
		/* New SQL is the same length as old */
		memcpy(ptr, sql, newlength);
	}
	else if (length - 1 > newlength)
	{
		/* New SQL is shorter */
		memcpy(ptr, sql, newlength);
		GWBUF_RTRIM(orig, (length - 1) - newlength);
	}
	else
	{
		memcpy(ptr, sql, length - 1);
		addition = gwbuf_alloc(newlength - (length - 1));
		memcpy(GWBUF_DATA(addition), &sql[length - 1], newlength - (length - 1));
		ptr = GWBUF_DATA(orig);
		*ptr++ = (newlength + 1) & 0xff;
		*ptr++ = ((newlength + 1) >> 8) & 0xff;
		*ptr++ = ((newlength + 1) >> 16) & 0xff;
		orig->next = addition;
	}

	return orig;
}
