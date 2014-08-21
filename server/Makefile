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
# 14/06/13	Mark Riddoch		Initial implementation
# 17/06/13	Mark Riddoch		Addition of documentation and depend
# 					targets
# 18/06/13	Mark Riddoch		Addition of install target
# 21/06/13	Mark Riddoch		Addition of inih
# 08/07/13	Mark Riddoch		Addition of monitor modules
# 16/07/13	Mark Riddoch		Renamed things to match the new naming

include ../build_gateway.inc
include ../makefile.inc

DEST=$(HOME)/usr/local/skysql

all:
	(cd inih/extra ; make -f Makefile.static)
	(cd core ; touch depend.mk ; make)
	(cd modules/routing; touch depend.mk ;make)
	(cd modules/routing/readwritesplit; touch depend.mk ;make)
	(cd modules/protocol; touch depend.mk ;make)
	(cd modules/monitor; touch depend.mk ;make)
	(cd modules/filter; touch depend.mk ;make)

cleantests:
	$(MAKE) -C test cleantests

buildtests:
	$(MAKE) -C test	HAVE_SRV=$(HAVE_SRV) buildtests

testall: 
	$(MAKE) -C test HAVE_SRV=$(HAVE_SRV) testall

clean:
	(cd Documentation; $(DEL) html)
	(cd core; touch depend.mk ; make clean)
	(cd modules/routing; touch depend.mk ; make clean)
	(cd modules/protocol; touch depend.mk ; make clean)
	(cd modules/monitor; touch depend.mk ; make clean)
	(cd modules/filter; touch depend.mk ; make clean)

depend:
	(cd core; touch depend.mk ; make depend)
	(cd modules/routing; touch depend.mk ; make depend)
	(cd modules/protocol; touch depend.mk ; make depend)
	(cd modules/monitor; touch depend.mk ; make depend)
	(cd modules/filter; touch depend.mk ; make depend)

install:
	@mkdir -p $(DEST)
	@mkdir -p $(DEST)
	@mkdir -p $(DEST)/modules
	@mkdir -p $(DEST)/log
	@mkdir -p $(DEST)/etc
	@mkdir -p $(DEST)/mysql
	@mkdir -p $(DEST)/lib
	@mkdir -p $(DEST)/Documentation
	install -b MaxScale_template.cnf $(DEST)/etc
	install ../Documentation/*.pdf $(DEST)/Documentation
	(cd core; make DEST=$(DEST) install)
	(cd modules/routing; make DEST=$(DEST) install)
	(cd modules/protocol; make DEST=$(DEST) install)
	(cd modules/monitor; make DEST=$(DEST) install)
	(cd modules/filter; make DEST=$(DEST) install)
