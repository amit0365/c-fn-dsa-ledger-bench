#pragma once

#include <stddef.h>
#include <stdint.h>

/* APDU command instructions for the FN-DSA bench app. */
typedef enum {
    GET_VERSION  = 0x03,  /* return MAJOR.MINOR.PATCH */
    GET_APP_NAME = 0x04,  /* return app name string */
    INS_RUN_BENCH = 0x10, /* run N sign iterations, return elapsed_us + sig_hash */
    INS_KAT_CHECK = 0x11, /* sign known input, return raw sig + hash for host comparison */
    INS_PROVISION = 0x12  /* compute basis from sk, store in N_app_basis NVRAM */
} command_e;

/* App-global context: tracks bench state. */
typedef struct {
    uint32_t last_elapsed_us;  /* elapsed time of last bench run */
    uint32_t last_iters;       /* iteration count of last bench run */
    uint8_t  last_sig_hash[32]; /* SHA3-256(sig) of last produced signature */
    uint8_t  basis_provisioned;/* 1 if N_app_basis is populated, 0 otherwise */
} bench_ctx_t;
