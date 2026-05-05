#pragma once

/* APDU handlers for the FN-DSA bench app.
 * All three are deterministic (use hardcoded sk + seed) so the host
 * can compare cycle counts run-to-run and bit-compare the produced
 * signatures against a known good. */

/* INS_PROVISION: compute basis B from the hardcoded sk and write to
 * N_app_basis NVRAM. Sets N_storage.basis_valid = 1 atomically last.
 * Idempotent — safe to call repeatedly.
 * Output: 1 status byte + SW=0x9000 on success. */
int handler_provision_basis(void);

/* INS_RUN_BENCH: do iters successive sign operations with the hardcoded
 * msg + seed, using the precomputed N_app_basis. Reports total elapsed
 * microseconds. Requires N_storage.basis_valid == 1.
 * Input p1 = iteration count (1..255, default 10 if 0).
 * Output: elapsed_us (4 LE bytes) + last_sig_hash (32 bytes SHA3-256
 *         of the final signature). */
int handler_run_bench(uint8_t iters);

/* INS_KAT_CHECK: do exactly 1 sign with the hardcoded sk + msg + seed
 * and return the raw signature for host KAT comparison.
 * Output: full signature bytes (666 bytes at logn=9). */
int handler_kat_check(void);
