# This file is distributed as part of MaxScale.  It is free
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
# 13/06/14	Mark Riddoch		Initial implementation of MaxScale
#					client program
# 18/06/14	Mark Riddoch		Addition of conditional for histedit

include ../build_gateway.inc
include ../makefile.inc

ifeq ($(wildcard /usr/include/histedit.h), )
HISTLIB=
HISTFLAG=
else
HISTLIB=-ledit
HISTFLAG=-DHISTORY
endif

CC=cc

CFLAGS=-c -Wall -g $(HISTFLAG)

SRCS= maxadmin.c

HDRS= 

OBJ=$(SRCS:.c=.o)

LIBS=$(HISTLIB)

all:	maxadmin

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test buildtests

runtests:
	$(MAKE) -C test runtests

testall:
	$(MAKE) -C test testall 

maxadmin: $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LIBS) -o $@


.c.o:
	$(CC) $(CFLAGS) $< -o $@


clean:
	$(DEL) $(OBJ) maxadmin
	$(DEL) *.so

tags:
	ctags $(SRCS) $(HDRS)

depend:	
	@$(DEL) depend.mk
	cc -M $(CFLAGS) $(SRCS) > depend.mk

install: maxadmin
	@mkdir -p $(DEST)/bin
	install -D maxadmin $(DEST)/bin

include depend.mk
