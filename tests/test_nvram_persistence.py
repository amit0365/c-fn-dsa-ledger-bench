#!/usr/bin/env python3
"""
test_nvram_persistence.py — verify N_app_basis survives an emulator
restart by running Speculos twice with --save-nvram / --load-nvram.

Phase 1: launch Speculos with --save-nvram, send INS_PROVISION, stop.
         Speculos writes NVRAM contents to main_nvram.bin on exit.
Phase 2: re-launch Speculos with --load-nvram (and --save-nvram for
         next time). Send INS_RUN_BENCH WITHOUT calling INS_PROVISION
         first. If basis_valid is still 1 and the basis bytes are
         readable, sign succeeds with the canonical hash.

Speculos's NVRAM file convention:
  - default name: main_nvram.bin
  - default location: cwd inside container (= /speculos/apps when we
    mount the repo there)
  - on host: <repo-root>/main_nvram.bin
"""
import socket
import struct
import subprocess
import sys
import time
import os

CANONICAL_HASH = "4abaa2708713f88052d5d4efffe02ba701c6a2a143bf64c58a6568eca46672b7"
NVM_FILE_HOST = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "main_nvram.bin")
APDU_PORT = 9999


def send(apdu_hex: str) -> tuple[bytes, bytes]:
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(60)
    s.connect(("localhost", APDU_PORT))
    apdu = bytes.fromhex(apdu_hex)
    s.sendall(struct.pack(">I", len(apdu)) + apdu)
    n = struct.unpack(">I", s.recv(4))[0]
    rsp = b""
    while len(rsp) < n:
        c = s.recv(n - len(rsp))
        if not c: break
        rsp += c
    sw = s.recv(2)
    s.close()
    return rsp, sw


def copy_nvram_out():
    """Speculos writes main_nvram.bin to /speculos/ inside the container
    (its cwd, not our /speculos/apps mount). Copy it out before the
    container exits so the host file persists for the next run."""
    subprocess.run(
        ["docker", "cp", "speculos-persist:/speculos/main_nvram.bin",
         NVM_FILE_HOST],
        capture_output=True)


def stop_speculos():
    subprocess.run(["docker", "stop", "-t", "5", "speculos-persist"],
                    capture_output=True)
    subprocess.run(["docker", "rm", "-f", "speculos-persist"],
                    capture_output=True)


def copy_nvram_in():
    """Before Phase 2: copy the host-side nvram.bin INTO the container
    at /speculos/main_nvram.bin so --load-nvram picks it up."""
    subprocess.run(
        ["docker", "cp", NVM_FILE_HOST,
         "speculos-persist:/speculos/main_nvram.bin"],
        capture_output=True)


def launch_speculos(load: bool, save: bool) -> str:
    """Launch Speculos in background. Returns container ID.

    Mounts the host main_nvram.bin directly to /speculos/main_nvram.bin
    inside the container so --load-nvram reads it at startup AND
    nvm_write's writes-through-on-flush land there too. We pre-touch
    the file if missing so Docker's bind-mount has something to mount."""
    repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    # Ensure the host file exists (Docker bind-mount requires it).
    if not os.path.exists(NVM_FILE_HOST):
        open(NVM_FILE_HOST, "w").close()
    cmd = [
        "docker", "run", "--rm", "-d", "--name", "speculos-persist",
        "-v", f"{repo}:/speculos/apps",
        "-v", f"{NVM_FILE_HOST}:/speculos/main_nvram.bin",
        "-p", f"{APDU_PORT}:9999",
        "-p", "5000:5000",
        "ghcr.io/ledgerhq/speculos:latest",
        "--display", "headless",
        "--apdu-port", "9999",
        "--api-port", "5000",
        "--model", "flex",
    ]
    if load: cmd.append("--load-nvram")
    if save: cmd.append("--save-nvram")
    cmd += ["apps/build/flex/bin/app.elf"]
    cid = subprocess.check_output(cmd, text=True).strip()
    time.sleep(5)  # boot time
    return cid


def main():
    # Clean any previous state
    if os.path.exists(NVM_FILE_HOST):
        os.remove(NVM_FILE_HOST)
        print(f"  → removed stale {NVM_FILE_HOST}")
    stop_speculos()

    print("=" * 60)
    print("Phase 1: Fresh launch — INS_PROVISION populates NVRAM")
    print("=" * 60)
    launch_speculos(load=False, save=True)
    try:
        data, sw = send("e012000000")
        assert sw == b"\x90\x00" and data == b"\x01", f"PROVISION: {data.hex()} {sw.hex()}"
        print(f"  ✓ INS_PROVISION succeeded (data={data.hex()}, SW={sw.hex()})")

        data, sw = send("e010010000")
        assert sw == b"\x90\x00", f"BENCH failed: {sw.hex()}"
        sig_hash_p1 = data[3:35].hex()
        assert sig_hash_p1 == CANONICAL_HASH, f"hash mismatch: {sig_hash_p1}"
        print(f"  ✓ INS_RUN_BENCH (Phase 1) sig_hash = canonical")

        # NVRAM file is bind-mounted, so writes-through went directly
        # to the host file. No copy-out needed.
    finally:
        stop_speculos()

    if not os.path.exists(NVM_FILE_HOST):
        sys.exit(f"FAIL: {NVM_FILE_HOST} doesn't exist after Phase 1\n"
                 f"  → Speculos --save-nvram didn't write the file")
    nvm_size = os.path.getsize(NVM_FILE_HOST)
    print(f"  → Speculos saved NVRAM to {os.path.basename(NVM_FILE_HOST)} "
          f"({nvm_size} bytes)")

    print()
    print("=" * 60)
    print("Phase 2: Re-launch with --load-nvram — skip INS_PROVISION")
    print("=" * 60)
    launch_speculos(load=True, save=True)
    try:
        # NO INS_PROVISION this run. Go straight to bench.
        data, sw = send("e010010000")
        if sw == b"\x69\x85":
            sys.exit(f"FAIL: INS_RUN_BENCH returned 0x6985 (basis_valid=0)\n"
                     f"  → NVRAM persistence DID NOT work — basis was not\n"
                     f"    loaded from main_nvram.bin")
        assert sw == b"\x90\x00", f"BENCH failed: {sw.hex()}"
        sig_hash_p2 = data[3:35].hex()
        if sig_hash_p2 != sig_hash_p1:
            sys.exit(f"FAIL: hash differs across reboot\n"
                     f"  Phase 1: {sig_hash_p1}\n"
                     f"  Phase 2: {sig_hash_p2}")
        print(f"  ✓ INS_RUN_BENCH succeeded WITHOUT re-provisioning")
        print(f"  ✓ sig_hash bit-identical to Phase 1: {sig_hash_p2[:32]}...")
    finally:
        stop_speculos()

    print()
    print("=" * 60)
    print("RESULT: NVRAM persistence path validated end-to-end")
    print("=" * 60)
    print("  Phase 1: nvm_write committed N_app_basis to NVRAM (saved to disk)")
    print("  Speculos exited; main_nvram.bin survived on host")
    print("  Phase 2: re-launched with --load-nvram, basis_valid was 1,")
    print("           sign succeeded with bit-identical signature hash")
    print()
    print("  → On real Flex/Stax hardware this path is automatic — Flash IS")
    print("    physically non-volatile. The Speculos test validates that our")
    print("    nvm_write calls and N_app_basis declarations are syntactically")
    print("    correct and produce the right SDK semantics.")


if __name__ == "__main__":
    main()
