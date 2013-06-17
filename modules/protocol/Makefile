# This file is distributed as part of the SkySQL Gateway.  It is free
# software: you can redistribute it and/or modify it under the terms of the
# GNU General Public License as published by the Free Software Foundation,
# version 2.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Copyright SkySQL Ab 2013
#
# Revision History
# Date		Who			Description
# 13/06/2013	Mark Riddoch		Initial protocol module development
# 17/06/2013	Massimiliano Pinto	Added mysql_common top both libraries

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../include -I../../include
LDFLAGS=-shared
MYSQLCLIENTSRCS=mysql_client.c mysql_common.c
MYSQLCLIENTOBJ=$(MYSQLCLIENTSRCS:.c=.o)
MYSQLBACKENDSRCS=mysql_backend.c mysql_common.c
MYSQLBACKENDOBJ=$(MYSQLBACKENDSRCS:.c=.o)
SRCS=$(MYSQLCLIENTSRCS) $(MYSQLBACKENDSRCS)
OBJ=$(SRCS:.c=.o)
LIBS=

all:	libMySQLClient.so libMySQLBackend.so

libMySQLClient.so: $(MYSQLCLIENTOBJ)
	$(CC) $(LDFLAGS) $(MYSQLCLIENTOBJ) $(LIBS) -o $@

libMySQLBackend.so: $(MYSQLBACKENDOBJ)
	$(CC) $(LDFLAGS) $(MYSQLBACKENDOBJ) $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) libMySQLClient.so libMySQLBackend.so

tags:
	ctags $(SRCS) $(HDRS)
