#pragma once

#include <stdint.h>

#include "ux.h"
#include "io.h"
#include "types.h"
#include "constants.h"

/* App context (RAM, transient per-bench-run state). */
extern bench_ctx_t G_bench;

/* NVRAM persistent storage:
 *   N_storage  — small flag region (basis-valid bit + reserved)
 *   N_app_basis — 4n fpr precomputed basis at logn=9 (32n bytes = 16384 bytes)
 *
 * The basis is the load-bearing precomputed object for FNDSA_LOW_RAM:
 * fndsa_compute_basis() writes here once (via INS_PROVISION), then
 * fndsa_sign_with_basis_temp() reads from it on every sign.
 */
typedef struct {
    uint8_t basis_valid;  /* 0 = not provisioned, 1 = N_app_basis populated */
    uint8_t reserved[15];
} app_storage_t;

extern const app_storage_t N_storage_real;
#define N_storage (*(volatile app_storage_t *) PIC(&N_storage_real))

/* logn=9 (Falcon-512) basis = 4n fpr = 4*512*8 = 16384 bytes. */
#define APP_BASIS_BYTES (4u * 512u * 8u)
extern const uint8_t N_app_basis_real[APP_BASIS_BYTES];
#define N_app_basis ((const uint8_t *) PIC(&N_app_basis_real))

/* Per-sign scratch (FNDSA_LOW_RAM at logn=9: 37n+31 = 18,975 bytes).
   Lives in .bss; defined in app_main.c. Aligned to 8 for fpr. */
#define G_FNDSA_TMP_SIZE (((size_t)37u << 9) + 31u)
extern uint8_t G_fndsa_tmp[G_FNDSA_TMP_SIZE];
