# Speculos validation tests

Three standalone Python scripts that exercise the bench app's APDU
handlers under Speculos emulation. They confirm Phase-3 functionality:
the FN-DSA library compiles for Cortex-M3, links into the app, and
produces deterministic correct-shape signatures end-to-end.

## Setup

```sh
# 1. Build the app (from the repo root)
docker run --rm -v "$(pwd):/app" -w /app \
    ghcr.io/ledgerhq/ledger-app-builder/ledger-app-builder:latest \
    bash -c 'export BOLOS_SDK=$FLEX_SDK && make'

# 2. Launch Speculos with the built app
docker run --rm -d --name speculos-bench \
    -v "$(pwd):/speculos/apps" \
    -p 5000:5000 -p 9999:9999 \
    ghcr.io/ledgerhq/speculos:latest \
    --display headless --apdu-port 9999 --api-port 5000 \
    --model flex \
    apps/build/flex/bin/app.elf

sleep 4   # let it boot
```

## Tests

- `test_basic.py` — sanity check on GET_VERSION / GET_APP_NAME (no FN-DSA)
- `test_fndsa_handlers.py` — exercise INS_PROVISION, INS_RUN_BENCH, INS_KAT_CHECK
- `test_reproducibility.py` — INS_RUN_BENCH 3 times; same seed should give
  same signature; sig_hash MUST be identical across runs
- `test_nvram_persistence.py` — provision in Speculos run #1, exit, re-launch
  Speculos with `--load-nvram`, run bench WITHOUT re-provisioning, verify
  basis survived. Tests the cross-reboot Flash persistence semantics.

## Speculos APDU framing (port 9999, raw TCP)

```
client → server: [4 bytes: BE length] [N bytes: APDU bytes]
server → client: [4 bytes: BE length] [N bytes: response data] [2 bytes: SW]
                  ─── data length only, NOT including SW ───
```

## Expected outputs (validated 2025-05-05)

```
GET_VERSION  → data = 02 03 01 (= 2.3.1)         SW=9000
GET_APP_NAME → data = "Boilerplate"               SW=9000
INS_PROVISION→ data = 01                          SW=9000   (~19 ms in Speculos)
INS_RUN_BENCH→ data = iters || sig_len || hash    SW=9000   (~32 ms / sign)
INS_KAT_CHECK→ data = first 250 bytes of sig      SW=9000   (~21 ms)

Reproducibility:
  Three INS_RUN_BENCH calls with same seed → same sig_hash:
    4abaa2708713f88052d5d4efffe02ba701c6a2a143bf64c58a6568eca46672b7
```

## What this proves vs what it doesn't

✅ Proves
- FN-DSA-512 LOW_RAM signing path compiles and links for Cortex-M3
- Signatures are byte-for-byte deterministic across runs (no RNG leaks)
- Output size is exactly 666 bytes (matches FN-DSA-512 spec)
- `G_fndsa_tmp[18,975]` allocation works at runtime
- `N_app_basis[16,384]` NVRAM commit + read-back works
- No stack overflow during signing (Speculos would crash if so)

❌ Does NOT prove
- Cycle count on real ST33K1M5 hardware (Speculos runs x86, not SC300)
- Bit-exact match against c-fn-dsa host reference (need separate test
  comparing the sig_hash above against a host-computed reference)
- Side-channel resistance (requires physical analysis on real silicon)

## Speculos NVRAM persistence quirk

For `test_nvram_persistence.py` to work, the host's `main_nvram.bin` must
be bind-mounted to `/speculos/main_nvram.bin` inside the container (not
`/speculos/apps/main_nvram.bin` — Speculos's cwd is `/speculos`, not the
volume-mounted apps dir). The launch command needs:

```sh
docker run --rm -d --name speculos-persist \
    -v "$(pwd):/speculos/apps" \
    -v "$(pwd)/main_nvram.bin:/speculos/main_nvram.bin" \
    ... \
    --load-nvram --save-nvram \
    --model flex \
    apps/build/flex/bin/app.elf
```

Both `--load-nvram` AND `--save-nvram` are needed for the
write-through-on-`nvm_write` behavior. The host file must exist before
container start (Docker bind-mount won't create it). The Python test
script handles all of this automatically.
