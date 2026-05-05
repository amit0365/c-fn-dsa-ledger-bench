/*****************************************************************************
 *   FN-DSA Ledger bench app — APDU handler implementations.
 *
 *  Three handlers, all using a hardcoded test-vector secret key (KAT_f/g/F)
 *  embedded in flash. The host script around this driver measures wall-clock
 *  time per APDU exchange to derive cycles/sign on the actual SE.
 *
 *  Design rationale:
 *    - sk is hardcoded so no large APDU input is needed for provisioning;
 *      the host just calls INS_PROVISION and the app does everything.
 *    - msg + seed are hardcoded so the bench is deterministic across runs;
 *      sig hash is reproducible and host-comparable.
 *    - timing is host-side (BOLOS doesn't expose DWT/SysTick to apps); the
 *      bench loop just runs N signs and returns control. Host measures
 *      elapsed_us = T_apdu_response - T_apdu_request.
 *****************************************************************************/

#include <stdint.h>
#include <string.h>

#include "os.h"
#include "io.h"
#include "lcx_sha3.h"
#include "lcx_hash.h"

#include "../globals.h"
#include "run_bench.h"
#include "kat_data.h"

/* From the vendored FN-DSA library. */
#include "../fndsa/fndsa.h"
#include "../fndsa/inner.h"      /* trim_i8_encode (private but vendored) */

/* Forward declaration: trim_i8_encode lives in fndsa/codec.c.
   It packs an int8_t array using nbits per element. */
extern size_t trim_i8_encode(unsigned logn, const int8_t *src, unsigned nbits,
                              uint8_t *dst);

/* Hardcoded bench message + seed — matches the in-tree KAT_512 vector
   (test_sign.c). Using these makes the produced signature byte-for-byte
   reproducible across runs, which the host can compare to a known good. */
static const uint8_t BENCH_MSG[7] = "message";
static const uint8_t BENCH_SEED[40 + 56] = {
    /* 96-byte (nonce + sub-seed) — first 96 bytes of KAT_512_RND. */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* sub-seed (56 bytes; arbitrary but fixed for reproducibility) */
    0xCA, 0xFE, 0xBA, 0xBE, 0xDE, 0xAD, 0xBE, 0xEF,
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
    0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF,
    0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
    0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
    0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE,
};

/* Persistent NVRAM-backed app storage and basis (declared in globals.h,
   defined in app_main.c). */
extern const app_storage_t N_storage_real;
extern const uint8_t N_app_basis_real[APP_BASIS_BYTES];
extern uint8_t G_fndsa_tmp[];   /* declared in app_main.c */

/* Encoded sign_key (FNDSA_SIGN_KEY_SIZE(9) = 1281 bytes). Lives in .bss;
   populated transiently during provisioning. */
#define LOGN 9
#define ENCODED_SK_SIZE (1u + 384u + 384u + 512u)  /* = FNDSA_SIGN_KEY_SIZE(9) */
static uint8_t G_sk_encoded[ENCODED_SK_SIZE];

/* Encode the hardcoded KAT_f/g/F into G_sk_encoded as the FN-DSA wire
   format expected by fndsa_compute_basis() and fndsa_sign_*(). */
static void
build_encoded_sk(void) {
    G_sk_encoded[0] = 0x50 | LOGN;
    /* trim_i8_encode at logn=9 with nbits=6 produces (6 << 9) >> 3 = 384 bytes
       per array (matches FN-DSA-512's secret-key encoding). */
    (void)trim_i8_encode(LOGN, KAT_f, 6, G_sk_encoded + 1);
    (void)trim_i8_encode(LOGN, KAT_g, 6, G_sk_encoded + 1 + 384);
    /* F is stored uncompressed as raw int8_t (logn=9 uses nbits=8 for F). */
    memcpy(G_sk_encoded + 1 + 384 + 384, KAT_F, 512);
}

int handler_provision_basis(void) {
    build_encoded_sk();

    /* Compute basis into G_fndsa_tmp (it has 18,975 bytes, easily fits
       4n*8 = 16,384 bytes of basis at logn=9). */
    if (!fndsa_compute_basis(G_sk_encoded, sizeof G_sk_encoded,
                              G_fndsa_tmp, APP_BASIS_BYTES)) {
        return io_send_sw(0x6F00);   /* generic failure */
    }

    /* Persist basis to flash (NVRAM). nvm_write handles the slow flash
       erase+program cycle and survives power loss mid-write only at the
       page granularity; the caller (host) should re-call INS_PROVISION
       if N_storage.basis_valid stays 0 after a tear. */
    nvm_write((void *)N_app_basis, G_fndsa_tmp, APP_BASIS_BYTES);

    /* Atomic flag: only set basis_valid AFTER the basis bytes are
       committed. Order matters for tear-resistance — see inner.h's
       FNDSA_LOW_RAM block (the "atomic flag" recommendation). */
    uint8_t one = 1;
    nvm_write((void *)&N_storage.basis_valid, &one, 1);

    /* Status byte 0x01 = success + SW=0x9000. */
    G_io_apdu_buffer[0] = 0x01;
    return io_send_response_pointer(G_io_apdu_buffer, 1, 0x9000);
}

int handler_run_bench(uint8_t iters) {
    if (iters == 0) iters = 10;
    if (N_storage.basis_valid != 1) {
        return io_send_sw(0x6985);   /* conditions not satisfied */
    }

    build_encoded_sk();

    /* Sign N times. Output buffer reused; only final sig is preserved. */
    static uint8_t sig[FNDSA_SIGNATURE_SIZE(LOGN)];
    size_t last_sig_len = 0;
    for (uint8_t i = 0; i < iters; i++) {
        size_t l = fndsa_sign_seeded_with_basis_temp(
            G_sk_encoded, sizeof G_sk_encoded,
            N_app_basis,
            NULL, 0, FNDSA_HASH_ID_RAW, BENCH_MSG, sizeof BENCH_MSG,
            BENCH_SEED, sizeof BENCH_SEED,
            sig, sizeof sig,
            G_fndsa_tmp, G_FNDSA_TMP_SIZE);
        if (l == 0 || l > sizeof sig) {
            return io_send_sw(0x6F01);   /* sign failed */
        }
        last_sig_len = l;
    }

    /* Hash the final signature with SHA3-256 so the host can verify
       cross-run reproducibility without parsing 666 raw bytes. */
    uint8_t sig_hash[32];
    cx_sha3_t sha;
    if (cx_sha3_init_no_throw(&sha, 256) != CX_OK) {
        return io_send_sw(0x6F02);
    }
    if (cx_hash_no_throw((cx_hash_t *)&sha, CX_LAST, sig, last_sig_len,
                         sig_hash, 32) != CX_OK) {
        return io_send_sw(0x6F03);
    }

    /* Response: iters (1 byte) + sig_len (2 bytes LE) + sig_hash (32 bytes). */
    G_io_apdu_buffer[0] = iters;
    G_io_apdu_buffer[1] = (uint8_t)(last_sig_len & 0xFF);
    G_io_apdu_buffer[2] = (uint8_t)((last_sig_len >> 8) & 0xFF);
    memcpy(&G_io_apdu_buffer[3], sig_hash, 32);

    return io_send_response_pointer(G_io_apdu_buffer, 35, 0x9000);
}

int handler_kat_check(void) {
    if (N_storage.basis_valid != 1) {
        return io_send_sw(0x6985);
    }
    build_encoded_sk();

    static uint8_t sig[FNDSA_SIGNATURE_SIZE(LOGN)];
    size_t l = fndsa_sign_seeded_with_basis_temp(
        G_sk_encoded, sizeof G_sk_encoded,
        N_app_basis,
        NULL, 0, FNDSA_HASH_ID_RAW, BENCH_MSG, sizeof BENCH_MSG,
        BENCH_SEED, sizeof BENCH_SEED,
        sig, sizeof sig,
        G_fndsa_tmp, G_FNDSA_TMP_SIZE);
    if (l == 0 || l > sizeof sig) {
        return io_send_sw(0x6F01);
    }

    /* sig is 666 bytes at logn=9 — fits in one APDU response (BOLOS
       supports up to ~260 bytes per APDU; need to reply via BOLOS's
       extended-response framing). For now write the first 250 bytes
       and the host can call back for more if needed. Placeholder until
       multi-APDU response is wired in Phase 4. */
    size_t out_len = (l < 250) ? l : 250;
    memcpy(G_io_apdu_buffer, sig, out_len);
    return io_send_response_pointer(G_io_apdu_buffer, out_len, 0x9000);
}
