////////////////////////////////////////
// SKYSQL Backend
// By Massimiliano Pinto 2012
// SkySQL AB
////////////////////////////////////////

#include "skysql_gw.h"

#define SKYSQL_READ 0
#define SKYSQL_WRITE 1

int skysql_query_is_select(const char *query) {

	return SKYSQL_READ;
}

int skysql_ext_file_ver(void) {
	int ret = 13;
	return ret;
}

int select_random_slave_server(int nslaves) {
	int random_balancer = (int) ((nslaves) * (rand() / (RAND_MAX + 1.0)));
	return random_balancer;
}

int get_server_from_list(char **selected_host, int *selected_port, char *server_list, int num, apr_pool_t *p) {
	int ret = -1;
	int curr_srv = 0;
	char *next = NULL;
	char *tmp = NULL;
	int port;

	if (num == 0) {
		port = atoi(strchr(server_list, ':') + 1), sizeof(port);
		memcpy(selected_port, &port, sizeof(int));
		*selected_host = apr_pstrndup(p, server_list, strchr(server_list, ':') - server_list);	

		return 1;
	}

	next = server_list;

	while (curr_srv < num) {
		tmp = strchr(next, ',');
		if (tmp != NULL) {
			curr_srv++;
			next = tmp+1;
		} else {
			return -1;
		}
	
		if (curr_srv == num) {
			port = atoi(strchr(next, ':') + 1);
			memcpy(selected_port,  &port, sizeof(port));
			*selected_host = apr_pstrndup(p, next, strchr(next, ':') - next);
			ret = 0;

			break;
		}
	}

	return ret;
}
