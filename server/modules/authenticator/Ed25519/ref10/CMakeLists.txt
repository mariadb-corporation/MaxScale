add_library(ed25519ref10 STATIC open.c sign.c keypair.c
        fe_0.c fe_1.c fe_add.c fe_cmov.c fe_copy.c fe_frombytes.c fe_invert.c fe_isnegative.c fe_isnonzero.c
        fe_mul.c fe_neg.c fe_pow22523.c fe_sq.c fe_sq2.c fe_sub.c fe_tobytes.c
        ge_add.c ge_double_scalarmult.c ge_frombytes.c ge_madd.c ge_msub.c
        ge_p1p1_to_p2.c ge_p1p1_to_p3.c ge_p2_0.c ge_p2_dbl.c
        ge_p3_0.c ge_p3_dbl.c ge_p3_to_cached.c ge_p3_to_p2.c ge_p3_tobytes.c ge_precomp_0.c
        ge_scalarmult_base.c ge_sub.c  ge_tobytes.c
        sc_muladd.c sc_reduce.c
        internal/verify.c internal/hash.c)
target_include_directories(ed25519ref10 PRIVATE exports internal)
