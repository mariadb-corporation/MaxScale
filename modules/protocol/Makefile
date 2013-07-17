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
# 27/06/2013	Vilho Raatikka          Added logmanager-related libs and 
#                                       headers so that liblog_manager.so can 
#                                       be linked in.
# 09/07/2013	Massimiliano Pinto	Added the HTTPD protocol module
#
include ../../../build_gateway.inc

LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../include -I../../include -I$(LOGPATH) \
	-I$(UTILSPATH) -Wall -g

include ../../../makefile.inc

LDFLAGS=-shared -L$(LOGPATH) -Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH)
MYSQLCLIENTSRCS=mysql_client.c mysql_common.c
MYSQLCLIENTOBJ=$(MYSQLCLIENTSRCS:.c=.o)
MYSQLBACKENDSRCS=mysql_backend.c mysql_common.c
MYSQLBACKENDOBJ=$(MYSQLBACKENDSRCS:.c=.o)
TELNETDSRCS=telnetd.c
TELNETDOBJ=$(TELNETDSRCS:.c=.o)
HTTPDSRCS=httpd.c
HTTPDOBJ=$(HTTPDSRCS:.c=.o)
SRCS=$(MYSQLCLIENTSRCS) $(MYSQLBACKENDSRCS) $(TELNETDSRCS) $(HTTPDSRCS)
OBJ=$(SRCS:.c=.o)
LIBS=$(UTILSPATH)/skygw_utils.o 
MODULES=libMySQLClient.so libMySQLBackend.so libtelnetd.so libHTTPD.so

all:	$(MODULES)

libMySQLClient.so: $(MYSQLCLIENTOBJ)
	$(CC) $(LDFLAGS) $(MYSQLCLIENTOBJ) $(LIBS) -o $@

libMySQLBackend.so: $(MYSQLBACKENDOBJ)
	$(CC) $(LDFLAGS) $(MYSQLBACKENDOBJ) $(LIBS) -o $@

libtelnetd.so: $(TELNETDOBJ)
	$(CC) $(LDFLAGS) $(TELNETDOBJ) $(LIBS) -o $@

libHTTPD.so: $(HTTPDOBJ)
	$(CC) $(LDFLAGS) $(HTTPDOBJ) $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) $(MODULES)

tags:
	ctags $(SRCS) $(HDRS)

install: $(MODULES)
	install -D $(MODULES) $(DEST)/MaxScale/modules

depend: 
	rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

include depend.mk
