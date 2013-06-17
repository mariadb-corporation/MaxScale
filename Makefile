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

all:
	(cd core; make)
	(cd modules/routing; make)
	(cd modules/protocol; make)

clean:
	(cd Documentation; rm -rf html)
	(cd core; make clean)
	(cd modules/routing; make clean)
	(cd modules/protocol; make clean)

depend:
	(cd core; make depend)
	(cd modules/routing; make depend)

documentation:
	doxygen doxygate
