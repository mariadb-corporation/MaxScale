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
# 21/07/14	Mark Riddoch		Initial module development

include ../../../../build_gateway.inc

LOGPATH := $(ROOT_PATH)/log_manager
UTILSPATH := $(ROOT_PATH)/utils

CC=cc
CFLAGS=-c -fPIC -I/usr/include -I../../include -I../../../include -I$(LOGPATH) \
	-I$(UTILSPATH) -Wall -g

include ../../../../makefile.inc

LDFLAGS=-shared  -L$(LOGPATH) -Wl,-rpath,$(DEST)/lib \
	-Wl,-rpath,$(LOGPATH) -Wl,-rpath,$(UTILSPATH)

SRCS= hintfilter.c hintparser.c
OBJ=$(SRCS:.c=.o)
LIBS=$(UTILSPATH)/skygw_utils.o -lssl -llog_manager

libhintfilter.so: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ) libhintfilter.so

tags:
	ctags $(SRCS) $(HDRS)

depend:
	@rm -f depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

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
