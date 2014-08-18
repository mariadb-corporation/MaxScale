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
# 27/06/13	Mark Riddoch		Initial framework put in place

include ../../../../build_gateway.inc

LOGPATH 	:= $(ROOT_PATH)/log_manager
UTILSPATH 	:= $(ROOT_PATH)/utils
QCLASSPATH 	:= $(ROOT_PATH)/query_classifier

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../../include -I../../../include \
	-I$(LOGPATH) -I$(UTILSPATH) -I$(QCLASSPATH) \
	$(MYSQL_HEADERS) -Wall -g

include ../../../../makefile.inc

LDFLAGS=-shared -L$(LOGPATH) -L$(QCLASSPATH) -L$(EMBEDDED_LIB) \
	-Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH) -Wl,-rpath,$(QCLASSPATH) \
	-Wl,-rpath,$(EMBEDDED_LIB)

SRCS=readwritesplit.c
OBJ=$(SRCS:.c=.o)
LIBS=-lssl -pthread -llog_manager -lquery_classifier -lmysqld
MODULES=libreadwritesplit.so

all:	$(MODULES)

libreadwritesplit.so: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(UTILSPATH)/skygw_utils.o $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(DEL) $(OBJ) $(MODULES)

tags:
	ctags $(SRCS) $(HDRS)

depend:
	@$(DEL) depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

install: $(MODULES)
	install -D $(MODULES) $(DEST)/modules
	
cleantests:
	$(MAKE) -C test cleantest
	
testall:
	$(MAKE) -C test testall
	
buildtests:
	$(MAKE) -C test buildtests
	
runtests:
	$(MAKE) -C runtests


include depend.mk
