#include "secrets.h"

int main(int argc, char *argv[])
{   
	char enc_key[1 + AES_BLOCK_SIZE * 2]="";
	char iv[1 + AES_BLOCK_SIZE]="";
	char *home =NULL;

	char secret_file[1024]="";

	if ((home = getenv("MAXSCALE_HOME")) != NULL) {
		sprintf(secret_file, "%s/secrets.key", home);
	} else {
		strcpy(secret_file, "./secrets.key");
	}

	secrets_readKeys(enc_key, iv, secret_file);

	fprintf(stderr, "<< Secret 32 is [%s]\n", enc_key);
	fprintf(stderr, "<< Iv 16 is [%s]\n", iv);
	

	exit(0);
}
