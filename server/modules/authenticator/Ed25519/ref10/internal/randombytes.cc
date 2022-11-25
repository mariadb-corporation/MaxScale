#include "randombytes.h"
#include <maxbase/worker.hh>

void randombytes(unsigned char* x, unsigned long long xlen)
{
    mxb::Worker::gen_random_bytes(x, xlen);
}
