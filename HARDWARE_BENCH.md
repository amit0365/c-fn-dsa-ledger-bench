# Running the bench on real Ledger Flex / Stax hardware

**Audience:** anyone with a Flex or Stax who agreed to flash a small
test app and report back numbers. **You don't need to build anything
or install developer tools** — just download two files, install the
prebuilt app, run a Python script, paste the output back.

Estimated time: **~10 minutes** including install.

## Files you need to download

From whoever asked you to do this (links / attachment / Slack file share):

| File | Purpose |
|---|---|
| **`app.apdu`** | The prebuilt Ledger app install bundle. Choose `flex` or `stax` matching your device. |
| **`run_hardware_bench.py`** | Python script that talks to the device via USB and runs the bench. |

That's it. Two files.

You don't need the `.elf`, the source code, Docker, or any toolchain.

## Step 1 — install the .apdu on your device

```sh
# One-time tool install (Python package)
pip install ledgerblue hidapi

# On macOS only, you also need the native USB lib:
brew install hidapi    # macOS
# (Linux: sudo apt install libhidapi-dev)
```

Connect your Flex / Stax via USB. **Make sure Ledger Live is closed
or in dashboard mode** (the bench can't run while Ledger Live holds the
USB lock).

Enable developer mode:
1. On the device, **Settings → Developer → Developer mode → ON**
2. Confirm any "unsafe manager" prompts that appear

Now flash the app:

```sh
python3 -m ledgerblue.runScript --apdu < app.apdu
```

The device will show a "Allow installation" prompt — confirm it.
Installation takes ~5 seconds. After install, the device returns to
the home screen with a new app icon ("Boilerplate" — that's the default
name we forgot to change; cosmetic only).

## Step 2 — open the app on the device

Scroll to **Boilerplate** on the device home screen and tap to open.

You should see:
> **FN-DSA Bench**
> FN-DSA-512 / FNDSA_LOW_RAM bench harness — APDU-driven.

The device is now waiting for APDU commands.

## Step 3 — run the bench script

```sh
python3 run_hardware_bench.py
```

The script:
1. Auto-detects your device (Flex / Stax / Nano)
2. Sends `INS_PROVISION` once — computes a precomputed-basis from the
   hardcoded test sk and persists it to NVRAM (~1 second)
3. Sends `INS_RUN_BENCH` with N=10 — signs 10 times, times the
   round-trip, computes per-sign average
4. Sends `INS_KAT_CHECK` — returns the first 32 bytes of the raw signature
5. Repeats `INS_RUN_BENCH` 3 more times to verify the signature is
   reproducible (same hardcoded seed → same signature, every time)
6. Prints a summary

Expected output looks like this (numbers are placeholders — actual
cycle count is what we're measuring and don't yet know):

```
Device: Ledger Flex
App version: 2.3.1
Provisioning basis ... done (855 ms)
Bench: 10 signatures with FN-DSA-512 LOW_RAM
  Total elapsed: 12,847 ms  (10 sigs)
  Per-sign average: 1,284.7 ms
  Signature length: 666 bytes  (expected 666)
  Sig hash: 4abaa2708713f88052d5d4efffe02ba701c6a2a143bf64c58a6568eca46672b7
KAT signature first 32 bytes: 3927c1bf1c440364bb1070af0e9c315690b6c71e4a90d988b4802861668751591

Reproducibility check: re-running INS_RUN_BENCH 3 more times...
  Run 1: hash = 4abaa270...  ✓
  Run 2: hash = 4abaa270...  ✓
  Run 3: hash = 4abaa270...  ✓

============================================================
Summary to send back:
============================================================
  Device:           Ledger Flex
  App version:      2.3.1
  Per-sign avg:     1,284.7 ms
  Sig hash:         4abaa2708713f88052d5d4efffe02ba701c6a2a143bf64c58a6568eca46672b7
  Provision time:   855 ms
  Reproducible:     yes (3 runs identical)
```

## Step 4 — send the output back

Copy the "Summary to send back" block and paste it into the channel
where this was requested (issue / chat / email).

We need:
- **Per-sign avg** — the headline number we're after
- **Sig hash** — confirms cryptographic correctness; should match what
  we measured in the Speculos emulator: `4abaa270...`
- **Device model** (Flex or Stax)
- **Reproducible: yes/no**

That's it. You can uninstall the app afterwards via Ledger Live →
Manager → Boilerplate → uninstall.

---

## Troubleshooting

**"No Ledger device found"**
→ Device is locked, or Ledger Live is open. Unlock the device, close
   Ledger Live, retry.

**Bench app open on device but script can't connect**
→ macOS may need explicit USB-HID permission. System Preferences →
   Security & Privacy → Input Monitoring → enable for Terminal.
   Or try `sudo python3 run_hardware_bench.py`.

**`ImportError: hidapi`**
→ macOS: `brew install hidapi` then `pip install hidapi`.
→ Linux: `sudo apt install libhidapi-dev` then `pip install hidapi`.

**Install fails with "Allow unsafe manager?"**
→ Tap allow on the device. Developer-mode apps trigger this prompt;
   it's expected.

**Device asks for PIN repeatedly during install**
→ Normal — Ledger requires PIN re-confirmation for app installs.

---

## What this app does (transparency)

The .apdu installs a Ledger app called "Boilerplate" that:

- Has **no real secret-key handling** — uses a hardcoded test vector
  (`KAT_512_f/g/F` from the c-fn-dsa test suite) baked into the app binary
- Performs **no transactions, transfers, or any operation that touches
  real assets**
- Stores 16 KiB of precomputed basis data in the app's own NVRAM
  (deleted when the app is uninstalled)
- Only responds to specific APDU commands; doesn't expose any other
  attack surface
- Built from source at: `<repo URL>` — full source vendored from
  c-fn-dsa with `FNDSA_LOW_RAM=1`

You can verify the .apdu hash against the published value before
installing if you want belt-and-suspenders. The hash is in the README
and in `bin/app.sha256` from the same release.

If you're reluctant to install a developer-mode app on your main
device, this is also flashable on a backup / spare Flex / Stax with
no real assets. The app is purely for benchmarking — no real-world
state is touched.

---

## For the requester (the person who asked you to run this)

If you're the one preparing this for a recipient, before sending:

1. Build the right .apdu for their device:
   ```sh
   docker run --rm -v "$(pwd):/app" -w /app \
       ghcr.io/ledgerhq/ledger-app-builder/ledger-app-builder:latest \
       bash -c 'export BOLOS_SDK=$FLEX_SDK && make'
   # → produces bin/app.apdu, build/flex/bin/app.elf, build/flex/bin/app.sha256

   # Repeat for Stax with $STAX_SDK if needed.
   ```
2. Send `bin/app.apdu` + `tests/run_hardware_bench.py` (just those 2 files)
3. Optionally: publish a SHA-256 of the .apdu for the recipient to verify
4. Direct them to this document
