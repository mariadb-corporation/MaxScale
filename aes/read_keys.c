#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include <openssl/aes.h>

#define MAXSCALE_SECRETS_ONE 4
#define MAXSCALE_SECRETS_TWO 28
#define MAXSCALE_SECRETS_INIT_VAL_ONE 11
#define MAXSCALE_SECRETS_INIT_VAL_TWO 5

int main(int argc, char *argv[])
{   
	char enc_key[1 + AES_BLOCK_SIZE * 2]="";
	char iv[1 + AES_BLOCK_SIZE]="";
	char *home =NULL;
	struct stat secret_stats;
	char read_buffer[1 + AES_BLOCK_SIZE * 2 + AES_BLOCK_SIZE + 3]="";

	char one_byte[1]="";
	char two_bytes[2]="";

	char secret_file[1024]="";
	int fd =0;
	int secret_file_size = 0;

	if ((home = getenv("MAXSCALE_HOME")) != NULL) {
		sprintf(secret_file, "%s/secrets.key", home);
	} else {
		strcpy(secret_file, "./secrets.key");
	}

	fd = open(secret_file, O_RDONLY);

	if (fd < 0) {
		fprintf(stderr, "%s, failed opening secret file [%s]. Error %i, %s\n", argv[0], secret_file, errno, strerror(errno));

	}

	if (fstat(fd, &secret_stats) < 0) {
		fprintf(stderr, "%s, failed accessing secret file details [%s]. Error %i, %s\n", argv[0], secret_file, errno, strerror(errno));
	}	

	secret_file_size = secret_stats.st_size;

	fprintf(stderr, "The secret file has %i bytes\n", secret_file_size);

	if (read(fd, read_buffer, sizeof(read_buffer)-1) < 0) {
		fprintf(stderr, "%s, failed reading from  secret file [%s]. Error %i, %s\n", argv[0], secret_file, errno, strerror(errno));
	}

	fprintf(stderr, "The file content is [%s]\n", read_buffer);

	memcpy(enc_key, read_buffer+1, MAXSCALE_SECRETS_ONE);

	memcpy(iv, read_buffer+1+MAXSCALE_SECRETS_ONE, MAXSCALE_SECRETS_INIT_VAL_ONE);

	memcpy(enc_key+ MAXSCALE_SECRETS_ONE, read_buffer+1+MAXSCALE_SECRETS_ONE+MAXSCALE_SECRETS_INIT_VAL_ONE, MAXSCALE_SECRETS_TWO);
	memcpy(iv+MAXSCALE_SECRETS_INIT_VAL_ONE, read_buffer+1+MAXSCALE_SECRETS_ONE+MAXSCALE_SECRETS_INIT_VAL_ONE+MAXSCALE_SECRETS_TWO, MAXSCALE_SECRETS_INIT_VAL_TWO);

	fprintf(stderr, "<< Secret 32 is [%s]\n", enc_key);
	fprintf(stderr, "<< Iv 16 is [%s]\n", iv);
	
	if (close(fd) < 0) {
		fprintf(stderr, "%s, failed closing the secret file [%s]. Error %i, %s\n", argv[0], secret_file, errno, strerror(errno));
	}

	exit(0);
}
