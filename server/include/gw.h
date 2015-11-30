#ifndef _GW_HG
#define _GW_HG

/*
 * This file is distributed as part of the MariaDB Corporation MaxScale.  It is free
 * software: you can redistribute it and/or modify it under the terms of the
 * GNU General Public License as published by the Free Software Foundation,
 * version 2.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Copyright MariaDB Corporation Ab 2013-2014
 */


#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <gwdirs.h>

#define EXIT_FAILURE 1

// network buffer is 32K
#define MAX_BUFFER_SIZE 32768

/**
 * Configuration for send and receive socket buffer sizes for
 * backend and cleint connections.
 */
#define GW_BACKEND_SO_SNDBUF (128 * 1024)
#define GW_BACKEND_SO_RCVBUF (128 * 1024)
#define GW_CLIENT_SO_SNDBUF  (128 * 1024)
#define GW_CLIENT_SO_RCVBUF  (128 * 1024)

#define GW_NOINTR_CALL(A)       do { errno = 0; A; } while (errno == EINTR)
#define GW_MYSQL_LOOP_TIMEOUT 300000000
#define GW_MYSQL_READ 0
#define GW_MYSQL_WRITE 1

#define GW_MYSQL_PROTOCOL_VERSION 10 // version is 10
#define GW_MYSQL_HANDSHAKE_FILLER 0x00
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE1 0xff
#define GW_MYSQL_SERVER_CAPABILITIES_BYTE2 0xf7
#define GW_MYSQL_SERVER_LANGUAGE 0x08
#define GW_MYSQL_MAX_PACKET_LEN 0xffffffL;
#define GW_MYSQL_SCRAMBLE_SIZE 20

// debug for mysql_* functions
#define MYSQL_CONN_DEBUG
#undef MYSQL_CONN_DEBUG

#include "dcb.h"

bool gw_daemonize(void);
int  do_read_dcb(DCB *dcb);
void MySQLListener(int epfd, char *config_bind);
int  MySQLAccept(DCB *listener);
char *gw_strend(register const char *s);
int  do_read_dcb(DCB *dcb);
int  do_read_10(DCB *dcb, uint8_t *buffer);
int  MySQLWrite(DCB *dcb, GWBUF *queue);
int  setnonblocking(int fd);
int  gw_write(DCB *dcb, const void *buf, size_t nbytes);
int  gw_getsockerrno(int fd);
int  parse_bindconfig(char *, unsigned short, struct sockaddr_in *);
int setipaddress(struct in_addr *, char *);
char* get_libdir();
long get_processor_count();
#endif
