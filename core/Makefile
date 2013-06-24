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
# 24/06/13	Massimiliano Pinto	Addition of libmysqlclient and its includes
#					do this for launching the gateway:
#					export LD_LIBRARY_PATH=.:$LD_LIBRARY_PATH:/packages/mariadb-5.5.25/libmysql					

CC=cc
CFLAGS=-c -I/usr/include -I../include -I../inih -I/usr/include/mysql -Wall -g
LDFLAGS=-rdynamic
SRCS= atomic.c buffer.c spinlock.c gateway.c gateway_mysql_protocol.c gw_utils.c \
	utils.c dcb.c load_utils.c session.c service.c server.c poll.c config.c \
	users.c hashtable.c dbusers.c
HDRS= ../include/atomic.h ../include/buffer.h ../include/dcb.h \
	../include/gateway_mysql.h ../include/gw.h ../include/mysql_protocol.h \
	../include/session.h ../include/spinlock.h ../include/thread.h \
	../include/modules.h ../include/poll.h ../include/config.h ../include/users.h \
	../include/hashtable.h
OBJ=$(SRCS:.c=.o)
LIBS=-L../inih/extra -linih -lssl -lstdc++ -L/packages/mariadb-5.5.25/libmysql -lmysqlclient -lz -lm -lcrypto

gateway: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) gateway

tags:
	ctags $(SRCS) $(HDRS)

depend:	depend.mk

depend.mk: $(SRCS)
	@rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

install: gateway
	install -D $< $(DEST)

include depend.mk
