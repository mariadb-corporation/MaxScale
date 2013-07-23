#include "secrets.h"

int main(int argc, char *argv[])
{
	char secret_file[1024]="";
	char *home = NULL;

	if ((home = getenv("MAXSCALE_HOME")) != NULL) {
		sprintf(secret_file, "%s/secrets.key", home);
	} else {
		strcpy(secret_file, "./secrets.key");
	}

	secrets_writeKeys(secret_file);

	fprintf(stderr, "MaxScale secret keys initialized in %s\n", secret_file);

	exit(0);
}
