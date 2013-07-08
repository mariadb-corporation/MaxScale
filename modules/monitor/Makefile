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
# 08/07/13	Mark Riddoch		Initial implementation

include ../../../build_gateway.inc
LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils

CC=cc
CFLAGS=-c -fPIC -I. -I/usr/include -I../include -I../../include -I$(LOGPATH) \
	-I$(UTILSPATH) -I/usr/include/mysql -Wall -g
LDFLAGS=-shared  -L$(LOGPATH) -Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH)
MYSQLSRCS=mysql_mon.c
MYSQLOBJ=$(MYSQLSRCS:.c=.o)
SRCS=$(MYSQLSRCS)
OBJ=$(SRCS:.c=.o)
LIBS=$(UTILSPATH)/skygw_utils.o -llog_manager \
	-L/packages/mariadb-5.5.25/libmysql -lmysqlclient
MODULES=libmysqlmon.so


all:	$(MODULES)

libmysqlmon.so: $(MYSQLOBJ)
	$(CC) $(LDFLAGS) $(MYSQLOBJ) $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) $(MODULES)

tags:
	ctags $(SRCS) $(HDRS)

depend:
	@rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

install: $(MODULES)
	install -D $< $(DEST)/gateway/modules

include depend.mk
