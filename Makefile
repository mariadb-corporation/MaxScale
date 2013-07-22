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

DEST=$(HOME)/usr/local/skysql

all:
	(cd log_manager; make)
	(cd query_classifier; make)
	(cd epoll_v1.0; make)

clean:
	(cd log_manager; make clean)
	(cd query_classifier; make clean)
	(cd epoll_v1.0; make clean)

depend:
	(cd log_manager; make depend)
	(cd query_classifier; make depend)
	(cd epoll_v1.0; make depend)

install:
	(cd epoll_v1.0; make DEST=$(DEST) install)
	(cd log_manager; make DEST=$(DEST)install)
	(cd query_classifier; make DEST=$(DEST) install)
