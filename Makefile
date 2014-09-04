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
# 16/07/13	Mark Riddoch		Initial implementation

include build_gateway.inc

DEST=$(HOME)/usr/local/skysql

#
# A special build of MaxScale is done for tests. 
# HAVE_SRV carries information whether test MaxScale server
# is built already or not. 
# HAVE_SRV == Y when test server is built,
# HAVE_SRV == N when not.
# It prevents unnecessary recompilation and also clean-up
# in the middle of the test.
#
HAVE_SRV := N

.PHONY: buildtestserver

all:
	(cd log_manager; make)
	(cd query_classifier; make)
	(cd server; make)
	(cd client; make)

clean:
	(cd log_manager; make clean)
	(cd query_classifier; make clean)
	(cd server; make clean)
	(cd client; touch depend.mk; make clean)

depend:
	echo '#define MAXSCALE_VERSION "'`cat $(ROOT_PATH)/VERSION`'"' > $(ROOT_PATH)/server/include/version.h
	(cd log_manager; make depend)
	(cd query_classifier; make depend)
	(cd server; make depend)
	(cd client; touch depend.mk; make depend)

install:
	(cd server; make DEST=$(DEST) install)
	(cd log_manager; make DEST=$(DEST) install)
	(cd query_classifier; make DEST=$(DEST) install)
	(cd client; make DEST=$(DEST) install)

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test	buildtests

testall:
	$(MAKE) -C test HAVE_SRV=$(HAVE_SRV) testall

buildtestserver:
	$(MAKE) DEBUG=Y DYNLIB=Y DEST=$(ROOT_PATH)/server/test clean depend all install
$(eval HAVE_SRV := Y)

documentation:
	doxygen doxygate


