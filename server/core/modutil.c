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
 * @file modutil.c  - Implementation of useful routines for modules
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 04/06/14	Mark Riddoch		Initial implementation
 * 24/10/14	Massimiliano Pinto	Added modutil_send_mysql_err_packet, modutil_create_mysql_err_msg
 *
 * @endverbatim
 */
#include <buffer.h>
#include <string.h>
#include <mysql_client_server_protocol.h>
#include <modutil.h>
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
	*length += (*ptr++ << 16);
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
	*residual += (*ptr++ << 16);
        ptr += 2;  // Skip sequence id	and COM_QUERY byte
	*residual = *residual - 1;
	*length = GWBUF_LENGTH(buf) - 5;
	*residual -= *length;
	*sql = (char *)ptr;
	return 1;
}

/**
 * Calculate the length of MySQL packet and how much is missing from the GWBUF 
 * passed as parameter.
 * 
 * This routine assumes that there is only one MySQL packet in the buffer.
 * 
 * @param buf			buffer list including the query, may consist of 
 * 				multiple buffers
 * @param nbytes_missing	pointer to missing bytecount 
 * 
 * @return the length of MySQL packet and writes missing bytecount to 
 * nbytes_missing.
 */
int modutil_MySQL_query_len(
	GWBUF* buf,
	int*   nbytes_missing)
{
	int     len;
	int     buflen;
	
	if (!modutil_is_SQL(buf))
	{
		len = 0;
		goto retblock;
	}
	len = MYSQL_GET_PACKET_LEN((uint8_t *)GWBUF_DATA(buf)); 
	*nbytes_missing = len-1;
	buflen = gwbuf_length(buf);
	
	*nbytes_missing -= buflen-5;	
	
retblock:
	return len;
}


/**
 * Replace the contents of a GWBUF with the new SQL statement passed as a text string.
 * The routine takes care of the modification needed to the MySQL packet,
 * returning a GWBUF chain that can be used to send the data to a MySQL server
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
	length += (*ptr++ << 16);
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
		addition->gwbuf_type = orig->gwbuf_type;
		orig->next = addition;
	}

	return orig;
}


/**
 * Extract the SQL from a COM_QUERY packet and return in a NULL terminated buffer.
 * The buffer should be freed by the caller when it is no longer required.
 *
 * If the packet is not a COM_QUERY packet then the function will return NULL
 *
 * @param buf	The buffer chain
 * @return Null terminated string containing query text or NULL on error
 */
char *
modutil_get_SQL(GWBUF *buf)
{
unsigned int	len, length;
unsigned char	*ptr, *dptr, *rval = NULL;

	if (!modutil_is_SQL(buf))
		return rval;
	ptr = GWBUF_DATA(buf);
	length = *ptr++;
	length += (*ptr++ << 8);
	length += (*ptr++ << 16);

	if ((rval = (char *)malloc(length + 1)) == NULL)
		return NULL;
	dptr = rval;
        ptr += 2;  // Skip sequence id	and COM_QUERY byte
	len = GWBUF_LENGTH(buf) - 5;
	while (buf && length > 0)
	{
		int clen = length > len ? len : length;
		memcpy(dptr, ptr, clen);
		dptr += clen;
		length -= clen;
		buf = buf->next;
		if (buf)
		{
			ptr = GWBUF_DATA(buf);
			len = GWBUF_LENGTH(buf);
		}
	}
	*dptr = 0;
	return rval;
}

/**
 * Copy query string from GWBUF buffer to separate memory area.
 * 
 * @param buf   GWBUF buffer including the query
 * 
 * @return Plain text query if the packet type is COM_QUERY. Otherwise return 
 * a string including the packet type.
 */
char *
modutil_get_query(GWBUF *buf)
{
        uint8_t*           packet;
        mysql_server_cmd_t packet_type;
        size_t             len;
        char*              query_str = NULL;
        
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
                        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char *)malloc(len+1)) == NULL)
                        {
                                goto retblock;
                        }
                        memcpy(query_str, &packet[5], len);
                        memset(&query_str[len], 0, 1);
                        break;
                        
                default:
                        len = strlen(STRPACKETTYPE(packet_type))+1;
                        if (len < 1 || len > ~(size_t)0 - 1 || (query_str = (char *)malloc(len+1)) == NULL)
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


/**
 * create a GWBUFF with a MySQL ERR packet
 *
 * @param packet_number         MySQL protocol sequence number in the packet
 * @param in_affected_rows      MySQL affected rows
 * @param mysql_errno           The MySQL errno
 * @param sqlstate_msg          The MySQL State Message
 * @param mysql_message         The Error Message
 * @return      The allocated GWBUF or NULL on failure
*/
GWBUF *modutil_create_mysql_err_msg(
	int		packet_number,
	int		affected_rows,
	int		merrno,
	const char	*statemsg,
	const char	*msg)
{
	uint8_t		*outbuf = NULL;
	uint32_t		mysql_payload_size = 0;
	uint8_t		mysql_packet_header[4];
	uint8_t		*mysql_payload = NULL;
	uint8_t		field_count = 0;
	uint8_t		mysql_err[2];
	uint8_t		mysql_statemsg[6];
	unsigned int	mysql_errno = 0;
	const char	*mysql_error_msg = NULL;
	const char	*mysql_state = NULL;
	GWBUF		*errbuf = NULL;

	if (statemsg == NULL || msg == NULL)
	{
		return NULL;
	}
        mysql_errno = (unsigned int)merrno;
        mysql_error_msg = msg;
        mysql_state = statemsg;

        field_count = 0xff;

        gw_mysql_set_byte2(mysql_err, mysql_errno);

        mysql_statemsg[0]='#';
        memcpy(mysql_statemsg+1, mysql_state, 5);

        mysql_payload_size = sizeof(field_count) +
                                sizeof(mysql_err) +
                                sizeof(mysql_statemsg) +
                                strlen(mysql_error_msg);

        /* allocate memory for packet header + payload */
        errbuf = gwbuf_alloc(sizeof(mysql_packet_header) + mysql_payload_size);
        ss_dassert(errbuf != NULL);

        if (errbuf == NULL)
	{
                return NULL;
	}
        outbuf = GWBUF_DATA(errbuf);

        /** write packet header and packet number */
        gw_mysql_set_byte3(mysql_packet_header, mysql_payload_size);
        mysql_packet_header[3] = packet_number;

        /** write header */
        memcpy(outbuf, mysql_packet_header, sizeof(mysql_packet_header));

        mysql_payload = outbuf + sizeof(mysql_packet_header);

        /** write field */
        memcpy(mysql_payload, &field_count, sizeof(field_count));
        mysql_payload = mysql_payload + sizeof(field_count);

        /** write errno */
        memcpy(mysql_payload, mysql_err, sizeof(mysql_err));
        mysql_payload = mysql_payload + sizeof(mysql_err);

        /** write sqlstate */
        memcpy(mysql_payload, mysql_statemsg, sizeof(mysql_statemsg));
        mysql_payload = mysql_payload + sizeof(mysql_statemsg);

        /** write error message */
        memcpy(mysql_payload, mysql_error_msg, strlen(mysql_error_msg));

        return errbuf;
}

/**
 * modutil_send_mysql_err_packet
 *
 * Send a MySQL protocol Generic ERR message, to the dcb
 *
 * @param dcb 			The DCB to send the packet
 * @param packet_number 	MySQL protocol sequence number in the packet 
 * @param in_affected_rows	MySQL affected rows
 * @param mysql_errno		The MySQL errno
 * @param sqlstate_msg		The MySQL State Message
 * @param mysql_message		The Error Message
 * @return	0 for successful dcb write or 1 on failure
 *
 */
int modutil_send_mysql_err_packet (
	DCB		*dcb,
	int		packet_number,
	int		in_affected_rows,
	int		mysql_errno,
	const char	*sqlstate_msg,	
	const char	*mysql_message)
{
        GWBUF* buf;

        buf = modutil_create_mysql_err_msg(packet_number, in_affected_rows, mysql_errno, sqlstate_msg, mysql_message);
   
        return dcb->func.write(dcb, buf);
}

/**
 * Buffer contains at least one of the following:
 * complete [complete] [partial] mysql packet
 * 
 * return pointer to gwbuf containing a complete packet or
 *   NULL if no complete packet was found.
 */
GWBUF* modutil_get_next_MySQL_packet(
	GWBUF** p_readbuf)
{
	GWBUF*   packetbuf;
	GWBUF*   readbuf;
	size_t   buflen;
	size_t   packetlen;
	size_t   totalbuflen;
	uint8_t* data;
	size_t   nbytes_copied = 0;
	uint8_t* target;
	
	readbuf = *p_readbuf;
	
	if (readbuf == NULL)
	{
		packetbuf = NULL;
		goto return_packetbuf;
	}                
	CHK_GWBUF(readbuf);
	
	if (GWBUF_EMPTY(readbuf))
	{
		packetbuf = NULL;
		goto return_packetbuf;
	}        
	totalbuflen = gwbuf_length(readbuf);
	data        = (uint8_t *)GWBUF_DATA((readbuf));
	packetlen   = MYSQL_GET_PACKET_LEN(data)+4;
	
	/** packet is incomplete */
	if (packetlen > totalbuflen)
	{
		packetbuf = NULL;
		goto return_packetbuf;
	}
	
	packetbuf = gwbuf_alloc(packetlen);
	target    = GWBUF_DATA(packetbuf);
	packetbuf->gwbuf_type = readbuf->gwbuf_type; /*< Copy the type too */
	/**
	 * Copy first MySQL packet to packetbuf and leave posible other
	 * packets to read buffer.
	 */
	while (nbytes_copied < packetlen && totalbuflen > 0)
	{
		uint8_t* src = GWBUF_DATA((*p_readbuf));
		size_t   bytestocopy;
		
		buflen = GWBUF_LENGTH((*p_readbuf));
		bytestocopy = MIN(buflen,packetlen-nbytes_copied);
		
		memcpy(target+nbytes_copied, src, bytestocopy);
		*p_readbuf = gwbuf_consume((*p_readbuf), bytestocopy);
		totalbuflen = gwbuf_length((*p_readbuf));
		nbytes_copied += bytestocopy;
	}
	ss_dassert(buflen == 0 || nbytes_copied == packetlen);
	
return_packetbuf:
	return packetbuf;
}

/**
 * Parse the buffer and split complete packets into individual buffers.
 * Any partial packets are left in the old buffer.
 * @param p_readbuf Buffer to split
 * @return Head of the chain of complete packets
 */
GWBUF* modutil_get_complete_packets(GWBUF** p_readbuf)
{
    GWBUF *buff = NULL, *packet = NULL;
    
    while((packet = modutil_get_next_MySQL_packet(p_readbuf)) != NULL)
    {
        buff = gwbuf_append(buff,packet);
    }
    
    return buff;
}

/**
 * Count the number of EOF, OK or ERR packets in the buffer. Only complete
 * packets are inspected and the buffer is assumed to only contain whole packets.
 * If partial packets are in the buffer, they are ingnored. The caller must handle the
 * detection of partial packets in buffers.
 * @param reply Buffer to use
 * @param use_ok Whether the DEPRECATE_EOF flag is set
 * @param n_found If there were previous packets found 
 * @return Number of EOF packets
 */
int
modutil_count_signal_packets(GWBUF *reply, int use_ok, int n_found)
{
    unsigned char* ptr = (unsigned char*) reply->start;
    unsigned char* end = (unsigned char*) reply->end;
    unsigned char* prev = ptr;
    int pktlen, eof = 0, err = 0, found = n_found;
    int errlen = 0, eoflen = 0;
    int iserr = 0, iseof = 0;
    while(ptr < end)
    {

        pktlen = MYSQL_GET_PACKET_LEN(ptr) + 4;

        if((iserr = PTR_IS_ERR(ptr)) || (iseof = PTR_IS_EOF(ptr)))
        {
            if(iserr)
            {
                err++;
                errlen = pktlen;
            }
            else if(iseof)
            {
                eof++;
                eoflen = pktlen;
            }
        }
        
        if((ptr + pktlen) > end)
        {
            ptr = prev;    
            break;
        }
        
        prev = ptr;
        ptr += pktlen;
    }


    /*
     * If there were new EOF/ERR packets found, make sure that they are the last
     * packet in the buffer.
     */
    if((eof || err) && n_found)
    {
        if(err)
        {
            ptr -= errlen;
            if(!PTR_IS_ERR(ptr))
                err = 0;
        }
        else
        {
            ptr -= eoflen;
            if(!PTR_IS_EOF(ptr))
                eof = 0;
        }
    }

    return(eof + err);
}
