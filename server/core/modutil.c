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
#include <string.h>
#include <mysql_client_server_protocol.h>

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
 * The length returned is the complete length of the SQL, which may
 * be larger than the amount of data in this packet.
 *
 * @param	buf	The packet buffer
 * @param	sql	Pointer that is set to point at the SQL data
 * @param	length	Length of the SQL query data
 * @return	True if the packet is a COM_QUERY packet
 */
int
modutil_extract_SQL(GWBUF *buf, char **sql, int *length)
{
unsigned char	*ptr;

	if (!modutil_is_SQL(buf))
		return 0;
	ptr = GWBUF_DATA(buf);
	*length = *ptr++;
	*length += (*ptr++ << 8);
	*length += (*ptr++ << 8);
        ptr += 2;  // Skip sequence id	and COM_QUERY byte
	*length = *length - 1;
	*sql = (char *)ptr;
	return 1;
}

/**
 * Extract the SQL portion of a COM_QUERY packet
 *
 * NB This sets *sql to point into the packet and does not
 * allocate any new storage. The string pointed to by *sql is
 * not NULL terminated.
 *
 * The number of bytes pointed to *sql is returned in *length
 *
 * The remaining number of bytes required for the complete query string
 * are returned in *residual
 *
 * @param	buf		The packet buffer
 * @param	sql		Pointer that is set to point at the SQL data
 * @param	length		Length of the SQL query data pointed to by sql
 * @param	residual	Any remain part of the query in future packets
 * @return	True if the packet is a COM_QUERY packet
 */
int
modutil_MySQL_Query(GWBUF *buf, char **sql, int *length, int *residual)
{
unsigned char	*ptr;

	if (!modutil_is_SQL(buf))
		return 0;
	ptr = GWBUF_DATA(buf);
	*residual = *ptr++;
	*residual += (*ptr++ << 8);
	*residual += (*ptr++ << 8);
        ptr += 2;  // Skip sequence id	and COM_QUERY byte
	*residual = *residual - 1;
	*length = GWBUF_LENGTH(buf) - 5;
	*residual -= *length;
	*sql = (char *)ptr;
	return 1;
}



/**
 * Replace the contents of a GWBUF with the new SQL statement passed as a text string.
 * The routine takes care of the modification needed to the MySQL packet,
 * returning a GWBUF chian that cna be used to send the data to a MySQL server
 *
 * @param orig	The original request in a GWBUF
 * @param sql	The SQL text to replace in the packet
 * @return A newly formed GWBUF containing the MySQL packet.
 */
GWBUF *
modutil_replace_SQL(GWBUF *orig, char *sql)
{
unsigned char	*ptr;
int	length, newlength;
GWBUF	*addition;

	if (!modutil_is_SQL(orig))
		return NULL;
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

/**
 * Copy query string from GWBUF buffer to separate memory area.
 * 
 * @param buf   GWBUF buffer including the query
 * 
 * @return Plaint text query if the packet type is COM_QUERY. Otherwise return 
 * a string including the packet type.
 */
char* modutil_get_query(
        GWBUF* buf)
{
        uint8_t*           packet;
        mysql_server_cmd_t packet_type;
        size_t             len;
        char*              query_str;
        
        packet = GWBUF_DATA(buf);
        packet_type = packet[4];
        
        switch (packet_type) {
                case MYSQL_COM_QUIT:
                        len = strlen("[Quit msg]")+1;
                        if ((query_str = (char *)malloc(len+1)) == NULL)
                        {
                                goto retblock;
                        }
                        memcpy(query_str, "[Quit msg]", len);
                        memset(&query_str[len], 0, 1);
                        break;
                        
                case MYSQL_COM_QUERY:
                        len = MYSQL_GET_PACKET_LEN(packet)-1; /*< distract 1 for packet type byte */        
                        if ((query_str = (char *)malloc(len+1)) == NULL)
                        {
                                goto retblock;
                        }
                        memcpy(query_str, &packet[5], len);
                        memset(&query_str[len], 0, 1);
                        break;
                        
                default:
                        len = strlen(STRPACKETTYPE(packet_type))+1;
                        if ((query_str = (char *)malloc(len+1)) == NULL)
                        {
                                goto retblock;
                        }
                        memcpy(query_str, STRPACKETTYPE(packet_type), len);
                        memset(&query_str[len], 0, 1);
                        break;
        } /*< switch */
retblock:
        return query_str;
}
