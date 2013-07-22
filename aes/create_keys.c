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
	char secret_buffer[1 + AES_BLOCK_SIZE * 3 + 3] = "";
	char scramble_secret[1 + AES_BLOCK_SIZE * 3 + 3] = "";
	char enc_key[1 + AES_BLOCK_SIZE * 2]="";
	char iv[1 + AES_BLOCK_SIZE]="";
	char *home =NULL;

	char one_byte[1 + 1]="";
	char two_bytes[1 + 2]="";

	char secret_file[1024]="";
	int fd =0;

	if ((home = getenv("MAXSCALE_HOME")) != NULL) {
		sprintf(secret_file, "%s/etc/secrets.key");
	} else {
		strcpy(secret_file, "./secrets.key");
	}

	fd = open(secret_file, O_CREAT | O_WRONLY | O_TRUNC);

	if (fd < 0) {
		fprintf(stderr, "%s, failed opening secret file [%s]. Error %i, %s\n", argv[0], secret_file, errno, strerror(errno));
		exit(1);

	}

	srand(time(NULL));
	gw_generate_random_str(secret_buffer, AES_BLOCK_SIZE * 3 + 3);

	memcpy(one_byte, secret_buffer, 1);
	memcpy(enc_key, secret_buffer + 1, AES_BLOCK_SIZE * 2);
	memcpy(iv, secret_buffer + 1 + AES_BLOCK_SIZE * 2, AES_BLOCK_SIZE);
	memcpy(two_bytes, secret_buffer + 1 + AES_BLOCK_SIZE * 2 + AES_BLOCK_SIZE + 1, 2);

	//fprintf(stderr, "<<< Key32 is [%s]\n", enc_key);
	//fprintf(stderr, "<<< IV16 is [%s]\n", iv);

	memcpy(scramble_secret, one_byte, 1);

	memcpy(scramble_secret + 1, enc_key, MAXSCALE_SECRETS_ONE);

	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE, iv, MAXSCALE_SECRETS_INIT_VAL_ONE);

	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE + MAXSCALE_SECRETS_INIT_VAL_ONE, enc_key + MAXSCALE_SECRETS_ONE, MAXSCALE_SECRETS_TWO);

	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE + MAXSCALE_SECRETS_INIT_VAL_ONE + MAXSCALE_SECRETS_TWO, iv + MAXSCALE_SECRETS_INIT_VAL_ONE, MAXSCALE_SECRETS_INIT_VAL_TWO);

	memcpy(scramble_secret + 1 + MAXSCALE_SECRETS_ONE + MAXSCALE_SECRETS_INIT_VAL_ONE + MAXSCALE_SECRETS_TWO + MAXSCALE_SECRETS_INIT_VAL_TWO, two_bytes, 2);


	if(write(fd, scramble_secret, sizeof(scramble_secret)-1) < 0) {
		fprintf(stderr, "%s, failed writing into secret file [%s]. Error %i, %s\n", argv[0], secret_file, errno, strerror(errno));
		exit(1);
	}

	fprintf(stderr, "MaxScale secret keys initialized in %s\n", secret_file);

	if (close(fd) < 0) {
		fprintf(stderr, "%s, failed closing the secret file [%s]. Error %i, %s\n", argv[0], secret_file, errno, strerror(errno));
	}

	exit(0);
}
