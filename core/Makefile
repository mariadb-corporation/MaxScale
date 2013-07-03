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
# 13/06/13	Mark Riddoch		Addition of -rdynamic to allow libraries
# 					to resolve symbols in the main executable
# 17/06/13	Mark Riddoch		Addition of dependency generation
# 24/06/13	Massimiliano Pinto	Addition of libmysqlclient and its 
#                                       includes do this for launching the 
#                                       gateway: export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH:/packages/mariadb-5.5.25/libmysql					
# 27/06/13      Vilho Raatikka          Added logmanager-related libs and 
#                                       headers so that liblog_manager.so can 
#                                       be linked in.
# 28/06/13      Vilho Raatikka          Added query classifier-related libs and
#                                       commented out mysql client headers and 
#                                       lib to avoid conflicts. MARIADB_SRC_PATH
#                                       is set in build_gateway.inc. Examples
#                                       are behind SS_DEBUG macros.
# 29/06/13       Vilho Raatikka         Reverted Query classifier changes because
#                                       gateway needs mysql client lib, not qc.
# 03/07/13	 Vilho Raatikka		Added target 'links' which creates links
#                                       to libraries needed by gateway.

include ../../build_gateway.inc

LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils

CC=cc

CFLAGS=-c -I/usr/include -I../include -I../inih \
	-I/usr/include/mysql \
	-I$(LOGPATH) -I$(UTILSPATH) \
	-Wall -g


ifdef DEBUG
	CFLAGS := $(CFLAGS) -DSS_DEBUG
endif

LDFLAGS=-rdynamic -L$(LOGPATH) \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH)

SRCS= atomic.c buffer.c spinlock.c gateway.c gateway_mysql_protocol.c \
	gw_utils.c utils.c dcb.c load_utils.c session.c service.c server.c \
	poll.c config.c users.c hashtable.c dbusers.c thread.c gwbitmask.c

HDRS= ../include/atomic.h ../include/buffer.h ../include/dcb.h \
	../include/gateway_mysql.h ../include/gw.h ../include/mysql_protocol.h \
	../include/session.h ../include/spinlock.h ../include/thread.h \
	../include/modules.h ../include/poll.h ../include/config.h \
	../include/users.h ../include/hashtable.h ../include/gwbitmask.h
OBJ=$(SRCS:.c=.o)

LIBS=-L../inih/extra -linih -lssl -lstdc++ \
	-L/packages/mariadb-5.5.25/libmysql -lmysqlclient \
	-lz -lm -lcrypto -ldl -pthread -llog_manager

gateway: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(UTILSPATH)/skygw_utils.o $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) gateway
	rm *.so

tags:
	ctags $(SRCS) $(HDRS)

depend:	
	@rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

install: gateway
	install -D $< $(DEST)

links:
	ln -s $(ROOT_PATH)/epoll_v1.0/modules/routing/libdebugcli.so \
	./libdebugcli.so
	ln -s $(ROOT_PATH)/epoll_v1.0/modules/routing/libreadconnroute.so \
	libreadconnroute.so
	ln -s $(ROOT_PATH)/epoll_v1.0/modules/protocol/libMySQLClient.so \
	libMySQLClient.so
	ln -s $(ROOT_PATH)/epoll_v1.0/modules/protocol/libtelnetd.so \
	libtelnetd.so

include depend.mk
