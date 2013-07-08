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

DEST=/usr/local/skysql

all:
	(cd inih/extra ; make -f Makefile.static)
	(cd core; make)
	(cd modules/routing; make)
	(cd modules/protocol; make)
	(cd modules/monitor; make)

clean:
	(cd Documentation; rm -rf html)
	(cd core; make clean)
	(cd modules/routing; make clean)
	(cd modules/protocol; make clean)
	(cd modules/monitor; make clean)

depend:
	(cd core; make depend)
	(cd modules/routing; make depend)
	(cd modules/protocol; make depend)
	(cd modules/monitor; make depend)

documentation:
	doxygen doxygate

install:
	(cd core; make DEST=$(DEST) install)
	(cd modules/routing; make DEST=$(DEST) install)
	(cd modules/protocol; make DEST=$(DEST) install)
	(cd modules/moinitor; make DEST=$(DEST) install)
