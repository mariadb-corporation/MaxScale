# This file is distributed as part of MaxScale form MariaDB Corporation.  It is free
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
# Copyright MariaDB Corporation Ab 2014

include ../../../../build_gateway.inc

LOGPATH 	:= $(ROOT_PATH)/log_manager
UTILSPATH 	:= $(ROOT_PATH)/utils
QCLASSPATH 	:= $(ROOT_PATH)/query_classifier
COREPATH 	:= $(ROOT_PATH)/server/core

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../../include -I../../../include \
	-I$(LOGPATH) -I$(UTILSPATH) -I./ -I$(ROOT_PATH)/server/inih -I$(QCLASSPATH) \
	$(MYSQL_HEADERS) -Wall -g

include ../../../../makefile.inc

LDFLAGS=-rdynamic -L$(LOGPATH) -L$(UTILSPATH) -L$(EMBEDDED_LIB) \
	-Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH) \
	-Wl,-rpath,$(EMBEDDED_LIB)

SRCS=harness_util.c harness_common.c
OBJ=$(SRCS:.c=.o)
COREOBJ=$(COREPATH)/load_utils.o $(COREPATH)/dcb.o $(COREPATH)/utils.o \
	$(COREPATH)/gw_utils.o $(COREPATH)/buffer.o $(COREPATH)/poll.o \
	$(COREPATH)/spinlock.o	$(COREPATH)/gwbitmask.o $(COREPATH)/session.o \
	$(COREPATH)/atomic.o $(COREPATH)/hashtable.o $(COREPATH)/filter.o $(COREPATH)/modutil.o $(ROOT_PATH)/server/inih/ini.o \
	$(COREPATH)/hint.o $(COREPATH)/config.o $(COREPATH)/service.o $(COREPATH)/server.o $(COREPATH)/monitor.o $(COREPATH)/housekeeper.o $(COREPATH)/adminusers.o $(COREPATH)/dbusers.o $(COREPATH)/thread.o $(COREPATH)/users.o $(COREPATH)/secrets.o
LIBS= $(UTILSPATH)/skygw_utils.o -lssl -pthread -llog_manager -lmysqld -ldl -lcrypto -lcrypt -lm
MODULES := $(wildcard ../*.so)

all: build

build:$(OBJ)
	$(CC) $(OBJ) $(COREOBJ) $(LDFLAGS) $(LIBS) -o harness
	$(MAKE) -C ../
	cp ../*.so ./

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f *.o
	rm -f *.so
	rm -f harness
cleantests:clean
	rm *.output
buildtests:build

testall: 
	$(MAKE) cleantests
	$(MAKE) buildtests
	$(MAKE) runtests

runtests:
	@echo ""
	@echo "-------------------------------"
	@echo "$(shell date)"
	@echo "Test Filter harness"
	@echo "-------------------------------"
	@echo "Testing hints... "
	@./hint_tests.sh
	@echo ""

documentation:
	doxygen doxygen.conf
