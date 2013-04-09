////////////////////////////////////////
// SKYSQL Backend
// By Massimiliano Pinto 2012/2013
// SkySQL AB
////////////////////////////////////////

#include "skysql_gw.h"

int skysql_ext_file_ver(void) {
	int ret = 13;
	return ret;
}

//////////////////////////////////////////////////////////////
// The function takes the server list, 
// find the total server number
// and return a random selection for slaves (from total -1)
//////////////////////////////////////////////////////////////
int select_random_slave_server(const char *server_list, int *num_slaves) {
	int nslaves = 0;
	int random_balancer = 0;
	char *p = (char *)server_list;
	while( (p = strchr(p, ',')) != NULL) {
		p++;
		nslaves++;
	}

	memcpy(num_slaves, &nslaves, sizeof(int));

	if (nslaves == 1) {
		return 1;
	}

	// random selection
	random_balancer = (int) ((nslaves+1) * (rand() / (RAND_MAX + 1.0)));

	return random_balancer;
}

///////////////////////////////////////////////////////////////
// This takes a server from the list
// index 0 is always the Master
// the others refer to the salve,
// the slave number comes from: select_random_slave_server()
///////////////////////////////////////////////////////////////
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

			// the host string must be allocated in the memory pool!
			*selected_host = apr_pstrndup(p, next, strchr(next, ':') - next);
			ret = 0;

			break;
		}
	}

	return ret;
}


//////////////////////////////////////////////
// This funcion take the master from the list
// The index is always 0
//////////////////////////////////////////////
int get_master_from_list(char **selected_host, int *selected_port, char *server_list, apr_pool_t *p) {
	int ret = -1;
	int curr_srv = 0;
	char *next = NULL;
	char *tmp = NULL;
	int port;

	port = atoi(strchr(server_list, ':') + 1), sizeof(port);
	memcpy(selected_port, &port, sizeof(int));

	// the host string must be allocated in the memory pool!
	*selected_host = apr_pstrndup(p, server_list, strchr(server_list, ':') - server_list);	

	return 1;
}

///////////////////////////////////////
// Query Routing basic implementation
///////////////////////////////////////

int query_routing(const char *server_list, const char *sql_command, int procotol_command, int current_slave) {

	if (strstr(sql_command, "select ")) {
		// to the slave
		return SKYSQL_READ;
	} else {
		// to the master
		return SKYSQL_WRITE;
	}
}

//////////////////
