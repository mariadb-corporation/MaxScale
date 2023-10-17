#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int crypto_sign(unsigned char* sm,
  const unsigned char *m,unsigned long long mlen,
  const unsigned char *pw, unsigned long long pwlen
);

int crypto_sign_open(
  unsigned char* m,
  const unsigned char* sm,unsigned long long smlen,
  const unsigned char* pk
);

int crypto_sign_keypair(
  unsigned char* pk,const unsigned char* pw,unsigned long long pwlen
);
#ifdef __cplusplus
}
#endif
