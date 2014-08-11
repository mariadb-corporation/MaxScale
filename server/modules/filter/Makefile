# This file is distributed as part of MaxScale form SkySQL.  It is free
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
# Copyright SkySQL Ab 2014
#
# Revision History
# Date		Who			Description
# 29/05/14	Mark Riddoch		Initial module development

include ../../../build_gateway.inc

LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../include -I../../include -I$(LOGPATH) \
	-I$(UTILSPATH) -Wall -g

include ../../../makefile.inc

LDFLAGS=-shared  -L$(LOGPATH) -Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH)

TESTSRCS=testfilter.c
TESTOBJ=$(TESTSRCS:.c=.o)
QLASRCS=qlafilter.c
QLAOBJ=$(QLASRCS:.c=.o)
REGEXSRCS=regexfilter.c
REGEXOBJ=$(REGEXSRCS:.c=.o)
TOPNSRCS=topfilter.c
TOPNOBJ=$(TOPNSRCS:.c=.o)
TEESRCS=tee.c
TEEOBJ=$(TEESRCS:.c=.o)
MQSRCS=mqfilter.c
MQOBJ=$(MQSRCS:.c=.o)
SRCS=$(TESTSRCS) $(QLASRCS) $(REGEXSRCS) $(TOPNSRCS) $(TEESRCS) $(MQSRCS)
OBJ=$(SRCS:.c=.o)
LIBS=$(UTILSPATH)/skygw_utils.o -lssl -llog_manager -lrabbitmq
MODULES= libtestfilter.so libqlafilter.so libregexfilter.so libtopfilter.so libtee.so libmqfilter.so libhintfilter.so


all:	$(MODULES)

libtestfilter.so: $(TESTOBJ)
	$(CC) $(LDFLAGS) $(TESTOBJ) $(LIBS) -o $@

libmqfilter.so: $(MQOBJ)
	$(CC) $(LDFLAGS) $(MQOBJ) $(LIBS) -o $@

libqlafilter.so: $(QLAOBJ)
	$(CC) $(LDFLAGS) $(QLAOBJ) $(LIBS) -o $@

libregexfilter.so: $(REGEXOBJ)
	$(CC) $(LDFLAGS) $(REGEXOBJ) $(LIBS) -o $@

libtopfilter.so: $(TOPNOBJ)
	$(CC) $(LDFLAGS) $(TOPNOBJ) $(LIBS) -o $@

libtee.so: $(TEEOBJ)
	$(CC) $(LDFLAGS) $(TEEOBJ) $(LIBS) -o $@

libhintfilter.so: 
	(cd hint; touch depend.mk ; make; cp $@ ..)

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) $(MODULES)
	(cd hint; touch depend.mk; make clean)

tags:
	ctags $(SRCS) $(HDRS)
	(cd hint; touch depend.mk; make tags)

depend:
	@rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk
	(cd hint; touch depend.mk; make depend)

install: $(MODULES)
	install -D $(MODULES) $(DEST)/modules

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test DEBUG=Y buildtests

runtests:
	$(MAKE) -C test runtests

testall:
	$(MAKE) -C test testall

include depend.mk
