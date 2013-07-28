#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <mysql.h>
#ifndef bool
#define bool int
#endif
#include "table_replication_consistency.h"
#include "../log_manager/log_manager.h"

static char* server_options[] = {
	(char *)"jtest",
	(char *)"--datadir=/tmp",
	(char *)"--skip-innodb",
	(char *)"--default-storage-engine=myisam",
    NULL
};

const int num_elements = (sizeof(server_options) / sizeof(char *)) - 1;

static char* server_groups[] = { (char *)"libmysqld_server",
                                 (char *)"libmysqld_client",
                                 (char *)"libmysqld_server",
                                 (char *)"libmysqld_server", NULL };


int main(int argc, char** argv)
{

  int i=0,k=0;
  char *uri;
  replication_listener_t *mrl;
  int err=0;
  char *errstr=NULL;

  // This will initialize MySQL
  if (mysql_library_init(num_elements, server_options, server_groups)) {
	  printf("MySQL server init failed\n");
	  exit(2);
  }


  mrl = (replication_listener_t*)calloc(argc, sizeof(replication_listener_t));

  if (argc < 2) {
	  printf("Usage: Example <uri> [<uri> ...]\n");
	  exit(2);
  }

  for(i=0; i < argc; i++) {
    uri= argv[i];

    if ( strncmp("mysql://", uri, 8) == 0) {

	    mrl[k].server_url = (char *)malloc(strlen(uri)+1);
	    strcpy(mrl[k].server_url, uri);

	    if (k == 0) {
		    mrl[k].is_master = 1;
	    }
	    k++;

    }
  }

  const char *opts[] = {
	  (char *)"test",
	  (char *)"-g",
	  (char *)"/home/jan/",
	  NULL
  };

  skygw_logmanager_init(NULL, 3, (char **)&opts);

  err = tb_replication_consistency_init(mrl, k, 5, TBR_TRACE_DEBUG);

  if (err ) {
	  perror(NULL);
	  exit(1);
  }

  // This will allow the server to start
  for(;;) {
	  sleep(10);
  }

  err = tb_replication_consistency_shutdown(&errstr);

  if (*errstr) {
	  fprintf(stderr, "%s\n", errstr);
	  free(errstr);
  }

  exit(0);

}
