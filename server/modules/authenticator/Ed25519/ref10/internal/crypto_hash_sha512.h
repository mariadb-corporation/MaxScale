#pragma once
extern int crypto_hash_sha512_openssl(unsigned char *,const unsigned char *,unsigned long long);
#define crypto_hash_sha512 crypto_hash_sha512_openssl
