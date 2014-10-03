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

/**
 *
 * @verbatim
 * Revision History
 *
 * Date		Who			Description
 * 14/02/2014	Massimiliano Pinto	Initial implementation
 * 17/02/2014   Massimiliano Pinto	Added check ipv4
 * 03/10/2014   Massimiliano Pinto	Added check for wildcard hosts
 *
 * @endverbatim
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dcb.h>
#include <service.h>
#include <users.h>
#include <skygw_utils.h>
#include <log_manager.h>
#include <secrets.h>
#include <dbusers.h>

#include <arpa/inet.h>

extern int setipaddress();

int set_and_get_single_mysql_users_ipv4(char *username, unsigned long ipv4, char *password) {
        struct sockaddr_in serv_addr;
        MYSQL_USER_HOST key;
        MYSQL_USER_HOST find_key;
	USERS *mysql_users;
	char ret_ip[200]="";
	char *fetch_data;

	unsigned long fix_ipv4;

	if (ipv4 > UINT_MAX) {
		fix_ipv4 = UINT_MAX;
	} else {
		fix_ipv4 = ipv4;
	}	
	
	mysql_users = mysql_users_alloc();
        /* prepare the user@host data struct */
	memset(&key, 0, sizeof(key));
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	memcpy(&(serv_addr).sin_addr.s_addr, &fix_ipv4, sizeof(ipv4));

	key.user = username;
	memcpy(&key.ipv4, &serv_addr, sizeof(serv_addr));

	inet_ntop(AF_INET, &(serv_addr).sin_addr, ret_ip, INET_ADDRSTRLEN);

	fprintf(stderr, "IPv4 passed/fixed [%lu/%lu] is [%s]\n", ipv4,fix_ipv4, ret_ip);

	/* add user@host as key and passwd as value in the MySQL users hash table */
	if (!mysql_users_add(mysql_users, &key, password)) {
		fprintf(stderr, "Failed adding %s@%s(%lu)\n", username, ret_ip, fix_ipv4);
		return 1;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	memset(&find_key, 0, sizeof(find_key));

	find_key.user = username;
	memcpy(&(serv_addr).sin_addr.s_addr, &ipv4, sizeof(ipv4));

	memcpy(&find_key.ipv4, &serv_addr, sizeof(serv_addr));

	fetch_data = mysql_users_fetch(mysql_users, &find_key);

	users_free(mysql_users);

	if (!fetch_data)
		return 1;
	
	return 0;
}

int set_and_get_single_mysql_users(char *username, char *hostname, char *password) {
        struct sockaddr_in serv_addr;
        MYSQL_USER_HOST key;
        MYSQL_USER_HOST find_key;
	USERS *mysql_users;
	char ret_ip[200]="";
	char *fetch_data;
	
	mysql_users = mysql_users_alloc();
        /* prepare the user@host data struct */
	memset(&serv_addr, 0, sizeof(serv_addr));
	memset(&key, 0, sizeof(key));

	
	if (hostname)	
		if(!setipaddress(&serv_addr.sin_addr, hostname)) {
			fprintf(stderr, "setipaddress failed for host [%s]\n", hostname);
			return 1;
		}
	if (username)
		key.user = username;

	memcpy(&key.ipv4, &serv_addr, sizeof(serv_addr));

	inet_ntop(AF_INET, &(serv_addr).sin_addr, ret_ip, INET_ADDRSTRLEN);

	fprintf(stderr, "set/get [%s@%s]: IPV4 %lu is [%u].[%u].[%u].[%u]\n", username, hostname, (unsigned long) serv_addr.sin_addr.s_addr, serv_addr.sin_addr.s_addr&0xFF, (serv_addr.sin_addr.s_addr&0xFF00), (serv_addr.sin_addr.s_addr&0xFF0000), ((serv_addr.sin_addr.s_addr & 0xFF000000) / (256*256*256)));

	/* add user@host as key and passwd as value in the MySQL users hash table */
	if (!mysql_users_add(mysql_users, &key, password)) {
		fprintf(stderr, "mysql_users_add() failed for %s@%s\n", username, hostname);
		return 1;
	}

	memset(&serv_addr, 0, sizeof(serv_addr));
	memset(&find_key, 0, sizeof(key));

	if (hostname)
		if(!setipaddress(&serv_addr.sin_addr, hostname)) {
			fprintf(stderr, "setipaddress failed for host [%s]\n", hostname);
			return 1;
		}
	key.user = username;
	memcpy(&key.ipv4, &serv_addr, sizeof(serv_addr));

	fetch_data = mysql_users_fetch(mysql_users, &key);

	users_free(mysql_users);

	if (!fetch_data)
		return 1;
	
	return 0;
}

int set_and_get_mysql_users_wildcards(char *username, char *hostname, char *password, char *from) {
	USERS *mysql_users;
	int ret;
	struct sockaddr_in client_addr;
	DCB	*dcb;
	SERVICE *service;

	dcb = dcb_alloc(DCB_ROLE_INTERNAL);

	if (dcb == NULL) {
		fprintf(stderr, "dcb_alloc() failed\n");
		return 1;
	}
        if ((service = (SERVICE *)calloc(1, sizeof(SERVICE))) == NULL) {
		fprintf(stderr, "service_alloc() failed\n");
		return 1;
	}

        memset(&client_addr, 0, sizeof(client_addr));

        if (hostname) {
		if(!setipaddress(&client_addr.sin_addr, from)) {
			fprintf(stderr, "setipaddress failed for host [%s]\n", from);
			return 1;
		}
	}

	/* client IPv4 in raw data*/
	memcpy(&dcb->ipv4, (struct sockaddr_in *)&client_addr, sizeof(struct sockaddr_in));

	dcb->service = service;

	mysql_users = mysql_users_alloc();

	service->users = mysql_users;


	// the routine returns 1 on success
	ret = add_mysql_users_with_host_ipv4(mysql_users, username, hostname, password);
	if (!ret) {
		fprintf(stderr, "add_mysql_users_with_host_ipv4 passed(%s@%s, %s) FAILED\n", username, hostname, password);
		return 1;
	} else {
		char db_passwd[100]="";

		dcb->remote=strdup(from);
		//fprintf(stderr, "add_mysql_users_with_host_ipv4 passed(%s@%s, %s) OK\n", username, hostname, password);

		fprintf(stderr, "Checking '%s' @ '%s' against (%s@%s)\n", username, from, username, hostname);

		// returns 0 on success
		ret =  gw_find_mysql_user_password_sha1(username, db_passwd, dcb);
	}

	return ret;
}

int main() {
	int ret;
	int i = 0;
	int k = 0;
	time_t t;

	fprintf(stderr, "----------------\n");

	time(&t);
	fprintf(stderr, "%s\n", asctime(localtime(&t)));
	fprintf(stderr, ">>> Started MySQL load, set & get users@host\n");


	ret = set_and_get_single_mysql_users("pippo", "localhost", "xyz");
	assert(ret == 0);
	ret = set_and_get_single_mysql_users("pippo", "127.0.0.2", "xyz");
	assert(ret == 0);
	ret = set_and_get_single_mysql_users("pippo", "%", "xyz");
	assert(ret == 1);
	ret = set_and_get_single_mysql_users("rootuser", NULL, "wwwww");
	assert(ret == 0);
	ret = set_and_get_single_mysql_users("nullpwd", "this_host_does_not_exists", NULL);
	assert(ret == 1);
	ret = set_and_get_single_mysql_users("myuser", "345.-1.5.40997", "password");
	assert(ret == 1);
	ret = set_and_get_single_mysql_users(NULL, NULL, NULL);
	assert(ret == 1);
	ret = set_and_get_single_mysql_users_ipv4("negative", -467295, "_ncd");
	assert(ret == 1);
	ret = set_and_get_single_mysql_users_ipv4("extra", 0xFFFFFFFFFUL * 100, "JJcd");
	assert(ret == 1);
	ret = set_and_get_single_mysql_users_ipv4("aaapo", 0, "JJcd");
	assert(ret == 0);
	ret = set_and_get_single_mysql_users_ipv4(NULL, '\0', "JJcd");
	assert(ret == 1);

	for (i = 256*256*256; i <= 256*256*256 + 5; i++) {
		char user[129] = "";
		snprintf(user, 128, "user_%i", k);
		ret = set_and_get_single_mysql_users_ipv4(user, i, "JJcd");
		assert(ret == 0);
		k++;
	}

	ret = set_and_get_mysql_users_wildcards("pippo", "%", "one", "127.0.0.1");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "%", "", "127.0.0.1");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "%", "two", "192.168.2.2");
	if (!ret) fprintf(stderr, "\t-- Expecting ok\n");
	assert(ret == 0);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.168.1.%", "foo", "192.168.2.2");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.168.%.%", "foo", "192.168.2.2");
	if (!ret) fprintf(stderr, "\t-- Expecting ok\n");
	assert(ret == 0);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.%.%.%", "foo", "192.68.0.2");
	if (!ret) fprintf(stderr, "\t-- Expecting ok\n");
	assert(ret == 0);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.%.%.%", "foo", "192.0.0.2");
	if (!ret) fprintf(stderr, "\t-- Expecting ok\n");
	assert(ret == 0);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.0.%.%", "foo", "192.2.0.2");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.0.0.1", "foo", "192.0.0.2");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.0.%.%", "foo", "192.1.0.2");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.0.0.%", "foo", "192.3.2.1");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.0.%.%", "foo", "192.3.2.1");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.%.%.%", "foo", "192.3.2.1");
	if (!ret) fprintf(stderr, "\t-- Expecting ok\n");
	assert(ret == 0);

	ret = set_and_get_mysql_users_wildcards("pippo", "192.0.0.%", "foo", "192.134.0.2");
	if (ret) fprintf(stderr, "\t-- Expecting no match\n");
	assert(ret == 1);

	fprintf(stderr, "----------------\n");
	fprintf(stderr, "<<< Test completed\n");

	time(&t);
	fprintf(stderr, "%s\n", asctime(localtime(&t)));

	return 0;
}

