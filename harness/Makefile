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

CC=gcc
CFLAGS=-c -I/usr/include -I../include -I../server/include -I../utils -Wall -g


SRCS=harness.c
OBJ=$(SRCS:.c=.o)

all:$(OBJ)	
	$(CC) $(OBJ) -o harness

.c.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJ)
