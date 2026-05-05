# Running the bench on real Ledger Flex / Stax hardware

This guide is for someone with a physical Flex or Stax who can flash
the app and report back cycle counts. **You do not need to develop
or build anything — just install + run a Python script.**

Estimated time: ~10 minutes.

## What we're measuring

- Wall-clock time for FN-DSA-512 (Falcon-512) signing using the
  `FNDSA_LOW_RAM` reduction (~19 KiB per-sign scratch)
- Reproducibility: the signature should be byte-identical across runs
  with the same hardcoded sk + msg + seed
- KAT bit-exactness: signature hash should match host-side reference

The app is bench-only: no real key handling, no transaction signing,
no UI flow. It just answers APDUs.

## Step 1 — install the app

The build produces `bin/app.apdu` which is Ledger's install-bundle
format. To flash:

### Option A: Ledger Live (developer mode)

1. Open Ledger Live → **Settings → Developer mode** (toggle on)
2. Connect your Flex or Stax via USB
3. Enable Manager access on the device
4. From a terminal, run:
   ```sh
   pip install ledgerblue
   python3 -m ledgerblue.runScript --apdu < bin/app.apdu
   ```

### Option B: ledgerctl (more direct, if you've used it before)

```sh
pip install ledgerwallet
ledgerctl install bin/app.apdu
```

The app will appear on the device as **"Boilerplate"** (default app
name from the boilerplate template — cosmetic, will be renamed in a
later iteration).

## Step 2 — open the app on the device

On the device's home screen, scroll to "Boilerplate" and tap to open.
You should see a screen reading "FN-DSA Bench" with the subtitle
"FN-DSA-512 / FNDSA_LOW_RAM bench harness — APDU-driven."

The device is now waiting for APDU commands.

## Step 3 — run the bench from your computer

```sh
pip install hidapi  # one-time
python3 tests/run_hardware_bench.py
```

That script:
1. Connects to the device via USB HID
2. Sends `INS_PROVISION` once (computes basis, persists to NVRAM — ~1 second)
3. Sends `INS_RUN_BENCH` with N=10 iterations (signs 10 times)
4. Reports total elapsed wall-clock + per-sign average
5. Sends `INS_KAT_CHECK` once and shows the first bytes of the signature
6. Prints a summary that you can copy back to us

Expected output:

```
Device: Ledger Flex (or Stax)
Provisioning basis ... done (855 ms)
Bench: 10 signatures with FN-DSA-512 LOW_RAM
  Total elapsed: 12,847 ms
  Per-sign average: 1,285 ms
  Sig hash: 4abaa2708713f88052d5d4efffe02ba701c6a2a143bf64c58a6568eca46672b7
KAT signature first 32 bytes: 3927c1bf1c440364bb1070af0e9c315690b6c71e4a90d988b4802861668751591

Reproducibility check: re-running INS_RUN_BENCH 3 more times...
  Run 1: hash = 4abaa270... ✓ (identical)
  Run 2: hash = 4abaa270... ✓
  Run 3: hash = 4abaa270... ✓
```

(Numbers are illustrative — actual cycle count is what we're trying
to measure and don't know yet.)

## Step 4 — send results back

Copy the output from Step 3 and paste it into the issue / chat where
this was requested. The values we need are:

- **Per-sign average ms** (the headline number)
- **Sig hash** — confirms bit-exactness vs Speculos run
- **Device model** (Flex or Stax)
- **App version** as reported by GET_VERSION

That's it. The whole thing should take ~10 minutes including install.

## What we'll do with the data

The per-sign cycle count on real ST33K1M5 silicon answers the open
question from upstream c-fn-dsa LOW_RAM submission: *can FN-DSA-512
with the precomputed-basis API actually meet a typical embedded-SE
signing-latency budget?* The answer depends entirely on this number.

Speculos timing is not meaningful (it's x86 emulating ARM, ~10-100×
slower per ARM instruction than real silicon). Only physical hardware
gives us the true cycle count.

## Troubleshooting

**Device says "Allow unsafe manager?"** → tap allow; this is needed
to install non-app-store apps.

**"App not in app store"** → expected; this is a developer-mode app.

**Python script can't find device** → make sure the device is unlocked,
the bench app is open (not the home screen), and Ledger Live is closed
(it can hold the USB lock).

**`ImportError: hidapi`** → on macOS: `brew install hidapi` then
re-run `pip install hidapi`. On Linux: `sudo apt install libhidapi-dev`.

## Removing the app afterwards

In Ledger Live → Manager → "Boilerplate" → uninstall. This wipes
the precomputed basis from NVRAM along with the app code. ~1 second.
