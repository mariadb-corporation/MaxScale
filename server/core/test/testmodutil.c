/*
 * This file is distributed as part of MaxScale.  It is free
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
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 17-09-2014	Martin Brampton		Initial implementation
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <modutil.h>
#include <buffer.h>

/**
 * test1	Allocate a service and do lots of other things
 *
  */

static int
test1()
{
GWBUF   *buffer;
char    *(sql[100]);
int     result, length, residual;

        /* Poll tests */  
        ss_dfprintf(stderr,
                    "testmodutil : Rudimentary tests."); 
        buffer = gwbuf_alloc(100);
        ss_info_dassert(0 == modutil_is_SQL(buffer), "Default buffer should be diagnosed as not SQL");
        /* There would ideally be some straightforward way to create a SQL buffer? */
        ss_dfprintf(stderr, "\t..done\nExtract SQL from buffer");
        ss_info_dassert(0 == modutil_extract_SQL(buffer, sql, &length), "Default buffer should fail");
        ss_dfprintf(stderr, "\t..done\nExtract SQL from buffer different way?");
        ss_info_dassert(0 == modutil_MySQL_Query(buffer, sql, &length, &residual), "Default buffer should fail");
        ss_dfprintf(stderr, "\t..done\nReplace SQL in buffer");
        ss_info_dassert(0 == modutil_replace_SQL(buffer, "select * from some_table;"), "Default buffer should fail");
        ss_dfprintf(stderr, "\t..done\nTidy up.");
        gwbuf_free(buffer);
        ss_dfprintf(stderr, "\t..done\n");
		
	return 0;
        
}

int
test2()
{
GWBUF   *buffer;
char len = 128;
char query[129];

        buffer = gwbuf_alloc(132);
	ss_info_dassert((buffer != NULL),"Buffer should not be null");

	memset(query,';',128);
    memset(query+128,'\0',1);
	*((unsigned char*)buffer->start) = len;
	*((unsigned char*)buffer->start+1) = 0;
	*((unsigned char*)buffer->start+2) = 0;
	*((unsigned char*)buffer->start+3) = 1;
	*((unsigned char*)buffer->start+4) = 0x03;
	memcpy(buffer->start + 5,query,strlen(query));
	char* result = modutil_get_SQL(buffer);
	ss_dassert(strcmp(result,query) == 0);
	gwbuf_free(buffer);
	free(result);
        ss_dfprintf(stderr, "\t..done\n");
	return 0;

}

int main(int argc, char **argv)
{
int	result = 0;

	result += test1();
	result += test2();
	exit(result);
}


