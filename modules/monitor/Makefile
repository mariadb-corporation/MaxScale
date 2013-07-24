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
	-I$(UTILSPATH) -I$(MARIADB_SRC_PATH)/include/ -Wall -g
LDFLAGS=-shared  -L$(LOGPATH) -Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH) \
	-Wl,-rpath,$(MARIADB_SRC_PATH)/libmysqld
MYSQLSRCS=mysql_mon.c
MYSQLOBJ=$(MYSQLSRCS:.c=.o)
GALERASRCS=mysql_mon.c
GALERAOBJ=$(GALERASRCS:.c=.o)
SRCS=$(MYSQLSRCS)
OBJ=$(SRCS:.c=.o)
LIBS=$(UTILSPATH)/skygw_utils.o -llog_manager \
	-L$(MARIADB_SRC_PATH)/libmysqld -lmysqld
MODULES=libmysqlmon.so libgaleramon.so


all:	$(MODULES)

libmysqlmon.so: $(MYSQLOBJ)
	$(CC) $(LDFLAGS) $(MYSQLOBJ) $(LIBS) -o $@

libgaleramon.so: $(GALERAOBJ)
	$(CC) $(LDFLAGS) $(GALERAOBJ) $(LIBS) -o $@

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
	install -D $(MODULES) $(DEST)/MaxScale/modules

include depend.mk
