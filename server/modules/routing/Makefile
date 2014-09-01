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
# 13/06/13	Mark Riddoch		Initial routing module development
# 27/06/13      Vilho Raatikka          Added logmanager-related libs and 
#                                       headers so that liblog_manager.so can 
#                                       be linked in.
# 27/06/13	Mark Riddoch		Addition of read write splitter

include ../../../build_gateway.inc

LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../include -I../../include -I$(LOGPATH) \
	-I$(UTILSPATH) -Wall -g

include ../../../makefile.inc

LDFLAGS=-shared  -L$(LOGPATH) -Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH)

TESTSRCS=testroute.c
TESTOBJ=$(TESTSRCS:.c=.o)
READCONSRCS=readconnroute.c
READCONOBJ=$(READCONSRCS:.c=.o)
DEBUGCLISRCS=debugcli.c debugcmd.c
DEBUGCLIOBJ=$(DEBUGCLISRCS:.c=.o)
CLISRCS=cli.c debugcmd.c
CLIOBJ=$(CLISRCS:.c=.o)
SRCS=$(TESTSRCS) $(READCONSRCS) $(DEBUGCLISRCS) cli.c
OBJ=$(SRCS:.c=.o)
LIBS=$(UTILSPATH)/skygw_utils.o -lssl -llog_manager
MODULES= libdebugcli.so libreadconnroute.so libtestroute.so libcli.so


all:	$(MODULES)

libtestroute.so: $(TESTOBJ)
	$(CC) $(LDFLAGS) $(TESTOBJ) $(LIBS) -o $@

libreadconnroute.so: $(READCONOBJ)
	$(CC) $(LDFLAGS) $(READCONOBJ) $(LIBS) -o $@

libdebugcli.so: $(DEBUGCLIOBJ)
	$(CC) $(LDFLAGS) $(DEBUGCLIOBJ) $(LIBS) -o $@

libcli.so: $(CLIOBJ)
	$(CC) $(LDFLAGS) $(CLIOBJ) $(LIBS) -o $@

libreadwritesplit.so:
#	(cd readwritesplit; touch depend.mk ; make; cp $@ ..)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	$(DEL) $(OBJ) $(MODULES)
	(cd readwritesplit; touch depend.mk; make clean)

tags:
	ctags $(SRCS) $(HDRS)
	(cd readwritesplit; make tags)

depend:
	@$(DEL) depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk
	(cd readwritesplit; touch depend.mk ; make depend)

install: $(MODULES)
	install -D $(MODULES) $(DEST)/modules
	(cd readwritesplit; make DEST=$(DEST) install)

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test DEBUG=Y buildtests

runtests:
	$(MAKE) -C test runtests

testall:
	$(MAKE) -C test testall

include depend.mk
