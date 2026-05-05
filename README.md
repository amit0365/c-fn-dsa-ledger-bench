# c-fn-dsa Ledger bench app

A minimal Ledger app that measures **FN-DSA-512 (Falcon-512) signing
performance and memory usage** on Ledger Flex / Stax hardware, using
the c-fn-dsa library with the `FNDSA_LOW_RAM` reduction enabled
(precomputed-basis API + outer-level ffsamp ℓ₁₀ drop, 19 KiB per-sign
tmp[] vs baseline 30 KiB).

The app is intentionally bench-only: it ships hardcoded test-vector
secret-key material, exposes three APDU commands for provisioning +
signing + KAT verification, and runs without user interaction.

For the original Ledger app-boilerplate README (build/dev guide, etc.),
see [BOILERPLATE_README.md](BOILERPLATE_README.md).

## What this validates

| Question | Self-serve (no device) | Hardware |
|---|---|---|
| Does FN-DSA-512 LOW_RAM compile for Cortex-M3? | ✅ Phase 2 | — |
| Does it fit in Ledger Flex / Stax memory budget? | ✅ Phase 3 (link succeeds, stack budget OK) | — |
| Does it produce correct signatures? | ✅ Phase 4 (Speculos, deterministic output) | ✅ when flashed |
| Real cycle count on ST33K1M5 silicon? | ❌ (Speculos emulates on x86; timing not meaningful) | ✅ via [HARDWARE_BENCH.md](HARDWARE_BENCH.md) |

## Memory layout

Targets ST33K1M5 (Cortex-M3-class secure element, 64 KB total SRAM,
36 KB available to apps after BOLOS reservation):

```
.text  (Flash)        91 KiB    code: FN-DSA-512 sign path + bench harness
N_app_basis (NVRAM)   16 KiB    precomputed B = [[g,-f],[G,-F]] in FFT format
                                (Layer 2 of LOW_RAM — flash-resident)
G_fndsa_tmp (.bss)    18,975 B  per-sign scratch (37n+31 at logn=9)
G_sk_encoded (.bss)    1,281 B  encoded sk (FN-DSA wire format)
sig + bookkeeping     ~2 KiB
─────────────────────────────────
SRAM used             ~24 KiB    fits in 36 KiB envelope
Stack remaining       ~12 KiB    well above linker's 1500-byte minimum
```

## Build

```sh
docker run --rm -v "$(pwd):/app" -w /app \
    ghcr.io/ledgerhq/ledger-app-builder/ledger-app-builder:latest \
    bash -c 'export BOLOS_SDK=$FLEX_SDK && make'    # Flex
docker run --rm -v "$(pwd):/app" -w /app \
    ghcr.io/ledgerhq/ledger-app-builder/ledger-app-builder:latest \
    bash -c 'export BOLOS_SDK=$STAX_SDK && make'    # Stax
```

Output: `build/<target>/bin/app.elf` and `bin/app.apdu` (the install bundle).

## Test in Speculos (no hardware)

See [tests/SPECULOS_README.md](tests/SPECULOS_README.md). Exercises all
three APDU handlers, validates determinism, confirms the FN-DSA path
runs end-to-end on a Cortex-M3 emulator.

## Run on real hardware

See [HARDWARE_BENCH.md](HARDWARE_BENCH.md). Step-by-step for someone
with a Flex or Stax to flash this app and report back cycle counts.

## APDU protocol

CLA = `0xE0` for all commands. logn hardcoded to 9 (FN-DSA-512).

| INS | Command | Input | Output |
|---|---|---|---|
| `0x03` | GET_VERSION | none | 3 bytes `major.minor.patch` |
| `0x04` | GET_APP_NAME | none | app name string |
| `0x12` | INS_PROVISION | none | `0x01` on success — computes basis, persists to NVRAM |
| `0x10` | INS_RUN_BENCH | p1 = iters (1..255) | iters || sig_len (LE) || sig_hash (32B) |
| `0x11` | INS_KAT_CHECK | none | first 250 bytes of raw signature |

Status word `0x9000` on success.

## Files we wrote (vs vendored / boilerplate)

```
src/types.h               (rewrote)  APDU INS enum + bench_ctx_t
src/globals.h             (rewrote)  N_storage, N_app_basis, G_fndsa_tmp
src/app_main.c            (modified) global allocations + main loop
src/ui/menu_nbgl.c        (rewrote)  minimal home screen for Flex/Stax NBGL
src/apdu/dispatcher.c     (rewrote)  APDU routing
src/handler/run_bench.c   (new)      INS_PROVISION / INS_RUN_BENCH / INS_KAT_CHECK
src/handler/run_bench.h   (new)
src/handler/kat_data.h    (new)      KAT_512_f/g/F embedded test vectors
tests/test_*.py           (new)      Speculos validation scripts
tests/run_hardware_bench.py (new)    Hardware bench driver

src/fndsa/                (vendored from c-fn-dsa, FNDSA_LOW_RAM=1)
    codec.c, mq.c, sha3.c, sysrng.c, util.c
    sign.c, sign_core.c, sign_fpoly.c, sign_fpr.c, sign_sampler.c
    fndsa.h, inner.h, sign_inner.h

src/handler/get_*.c       (boilerplate, unchanged) GET_VERSION / GET_APP_NAME
```

## License

App boilerplate scaffolding: Apache 2.0 (Ledger SAS).
c-fn-dsa: per upstream.
