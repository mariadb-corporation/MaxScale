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

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../include -I../../include -Wall -g
LDFLAGS=-shared
TESTSRCS=testroute.c
TESTOBJ=$(TESTSRCS:.c=.o)
READCONSRCS=readconnroute.c
READCONOBJ=$(READCONSRCS:.c=.o)
DEBUGSRCS=debugcli.c
DEBUGCLIOBJ=$(DEBUGSRCS:.c=.o)
SRCS=$(TESTSRCS) $(READCONSRCS)
OBJ=$(SRCS:.c=.o)
LIBS=-lssl
MODULES=libtestroute.so libreadconnroute.so libdebugcli.so

all:	$(MODULES)

libtestroute.so: $(TESTOBJ)
	$(CC) $(LDFLAGS) $(TESTOBJ) $(LIBS) -o $@

libreadconnroute.so: $(READCONOBJ)
	$(CC) $(LDFLAGS) $(READCONOBJ) $(LIBS) -o $@

libdebugcli.so: $(DEBUGCLIOBJ)
	$(CC) $(LDFLAGS) $(DEBUGCLIOBJ) $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) $(MODULES)

tags:
	ctags $(SRCS) $(HDRS)

depend:	depend.mk

depend.mk: $(SRCS)
	@rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

install: $(MODULES)
	install -D $< $(DEST)/gateway/modules

include depend.mk
