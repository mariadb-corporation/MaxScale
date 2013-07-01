#include "table_replication_consistency.h"
#include <getopt.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>

int main(int argc, char** argv) 
{

  int i=0,k=0;
  char *uri;
  replication_listener_t *mrl;
  int err=0;

  mrl = (replication_listener_t*)calloc(argc, sizeof(replication_listener_t));

  if (argc < 2) {
	  printf("Usage: Example <uri> [<uri> ...]\n");
	  exit(2);
  }

  for(i=0; i < argc; i++) {
    uri= argv[i];

    if ( strncmp("mysql://", uri, 8) == 0) {

      mrl[i].server_url = uri;
      k++;

      if (argc == 1) {
	mrl[i].is_master = 1;
      }

    }
  }//end of outer while loop

  err = tb_replication_consistency_init(mrl, k, 5);

  if (err ) {
	  perror(NULL);
	  exit(1);
  }

  for(;;) {
	  sleep(3);
  }

  exit(0);

}
