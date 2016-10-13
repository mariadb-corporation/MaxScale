#ifndef _GW_HG
#define _GW_HG
/*
 * Copyright (c) 2016 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl.
 *
 * Change Date: 2019-07-01
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
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
#include <maxscale/gwdirs.h>

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
int  do_read_dcb(DCB *dcb);
int  do_read_10(DCB *dcb, uint8_t *buffer);
int  MySQLWrite(DCB *dcb, GWBUF *queue);
int  setnonblocking(int fd);
int  gw_getsockerrno(int fd);
int  parse_bindconfig(const char *, struct sockaddr_in *);
int setipaddress(struct in_addr *, char *);
char* get_libdir();
long get_processor_count();
void clean_up_pathname(char *path);
bool mxs_mkdir_all(const char *path, int mask);
#endif
