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

include ../../build_gateway.inc

LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils
QCLASSPATH := $(ROOT_PATH)/query_classifier

CC=cc
#CFLAGS=-c -I/usr/include -I../include -I../inih \
#-I/usr/include/mysql \

CFLAGS=-c -I/usr/include -I../include -I../inih \
	-I$(LOGPATH) -I$(UTILSPATH) -I$(QCLASSPATH) \
	-I$(MARIADB_SRC_PATH)/include \
	-Wall -g

ifdef DEBUG
	CFLAGS := $(CFLAGS) -DSS_DEBUG
endif

LDFLAGS=-rdynamic -L$(LOGPATH) -L$(QCLASSPATH) -L$(MARIADB_SRC_PATH)/libmysqld \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH) -Wl,-rpath,$(QCLASSPATH) \
	-Wl,-rpath,$(MARIADB_SRC_PATH)/libmysqld

SRCS= atomic.c buffer.c spinlock.c gateway.c gateway_mysql_protocol.c \
	gw_utils.c utils.c dcb.c load_utils.c session.c service.c server.c \
	poll.c config.c users.c hashtable.c dbusers.c thread.c

HDRS= ../include/atomic.h ../include/buffer.h ../include/dcb.h \
	../include/gateway_mysql.h ../include/gw.h ../include/mysql_protocol.h \
	../include/session.h ../include/spinlock.h ../include/thread.h \
	../include/modules.h ../include/poll.h ../include/config.h \
	../include/users.h ../include/hashtable.h 
OBJ=$(SRCS:.c=.o)

#LIBS=-L../inih/extra -linih -lssl -lstdc++ \
#	-L/packages/mariadb-5.5.25/libmysql -lmysqlclient \

LIBS=-L../inih/extra -linih -lssl -lstdc++ \
	-lz -lm -lcrypto -ldl -pthread -llog_manager -lquery_classifier -lmysqld

gateway: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(UTILSPATH)/skygw_utils.o $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) gateway

tags:
	ctags $(SRCS) $(HDRS)

depend:	
	@rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

install: gateway
	install -D $< $(DEST)

include depend.mk
