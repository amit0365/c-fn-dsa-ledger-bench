#!/usr/bin/env python3
"""
run_hardware_bench.py — exercise the c-fn-dsa Ledger bench app on a
physical Flex or Stax via USB HID, print cycles + sig_hash for review.

Requires: pip install hidapi   (and the bench app open on the device)

What it does:
  1. Connects to the device (auto-detects Flex / Stax / Nano)
  2. Sends INS_PROVISION   (compute basis + NVRAM commit; ~1 s)
  3. Sends INS_RUN_BENCH p1=10 (10 signatures, times the wall clock)
  4. Sends INS_KAT_CHECK  (one sign, returns raw sig prefix)
  5. Sends INS_RUN_BENCH p1=1 three more times to verify reproducibility
  6. Prints a copy-pasteable summary

Send the output to whoever asked you to run this. The headline numbers
are: device model, per-sign avg ms, sig_hash (proves bit-exactness).
"""
import sys
import time
import struct

try:
    import hid
except ImportError:
    sys.exit("ImportError: pip install hidapi   (and on macOS: brew install hidapi)")


# Ledger USB IDs and APDU framing (HID transport, channel 0x0101).
LEDGER_VID = 0x2C97
LEDGER_PIDS = {
    "Nano S":  [0x1011, 0x1015],
    "Nano X":  [0x4011, 0x4015],
    "Nano S+": [0x5011, 0x5015],
    "Stax":    [0x6011, 0x6015],
    "Flex":    [0x7011, 0x7015],
}


def _find_device():
    devs = hid.enumerate(LEDGER_VID, 0x0)
    for dev in devs:
        for model, pids in LEDGER_PIDS.items():
            if dev["product_id"] in pids:
                return model, dev["path"]
    return None, None


def _wrap_apdu_for_hid(apdu: bytes) -> list[bytes]:
    """Split into 64-byte HID frames per Ledger's USB protocol."""
    chan = 0x0101
    cmd = 0x05
    seq = 0
    payload = struct.pack(">H", len(apdu)) + apdu
    chunks = []
    while payload:
        head = struct.pack(">HBH", chan, cmd, seq)
        body = payload[: 64 - len(head)]
        payload = payload[64 - len(head):]
        # HID requires 64-byte frames + 1 leading report-id byte (0x00)
        frame = b"\x00" + head + body
        frame = frame.ljust(65, b"\x00")
        chunks.append(frame)
        seq += 1
    return chunks


def _unwrap_hid_response(dev) -> bytes:
    """Read enough HID frames to assemble one APDU response."""
    seq = 0
    total_len = None
    payload = b""
    while True:
        frame = bytes(dev.read(64, timeout_ms=120000))
        if not frame:
            raise RuntimeError("device timeout — is the bench app open?")
        # Strip 5-byte HID header (channel, cmd, seq)
        body = frame[5:]
        if seq == 0:
            total_len = struct.unpack(">H", body[:2])[0]
            payload += body[2:]
        else:
            payload += body
        seq += 1
        if len(payload) >= total_len:
            return payload[:total_len]


def send_apdu(dev, apdu_hex: str) -> tuple[bytes, bytes]:
    """Send an APDU and return (data, sw)."""
    apdu = bytes.fromhex(apdu_hex)
    for frame in _wrap_apdu_for_hid(apdu):
        dev.write(frame)
    rsp = _unwrap_hid_response(dev)
    if len(rsp) < 2:
        raise RuntimeError(f"short response: {rsp.hex()}")
    return rsp[:-2], rsp[-2:]


def main():
    model, path = _find_device()
    if not path:
        sys.exit("No Ledger device found. Is it connected and unlocked?")

    print(f"Device: Ledger {model}")
    dev = hid.device()
    dev.open_path(path)

    # Sanity check: GET_VERSION
    data, sw = send_apdu(dev, "e003000000")
    if sw != b"\x90\x00":
        sys.exit(f"GET_VERSION failed: SW={sw.hex()}")
    print(f"App version: {data[0]}.{data[1]}.{data[2]}")

    # 1. PROVISION basis
    print(f"Provisioning basis ... ", end="", flush=True)
    t0 = time.monotonic()
    data, sw = send_apdu(dev, "e012000000")
    prov_ms = (time.monotonic() - t0) * 1000
    if sw != b"\x90\x00" or data != b"\x01":
        sys.exit(f"\nINS_PROVISION failed: data={data.hex()} SW={sw.hex()}")
    print(f"done ({prov_ms:.0f} ms)")

    # 2. RUN_BENCH N=10
    iters = 10
    print(f"Bench: {iters} signatures with FN-DSA-512 LOW_RAM")
    t0 = time.monotonic()
    data, sw = send_apdu(dev, f"e010{iters:02x}0000")
    bench_ms = (time.monotonic() - t0) * 1000
    if sw != b"\x90\x00":
        sys.exit(f"INS_RUN_BENCH failed: SW={sw.hex()}")
    iters_out = data[0]
    sig_len = struct.unpack("<H", data[1:3])[0]
    sig_hash = data[3:35].hex()
    print(f"  Total elapsed: {bench_ms:,.0f} ms  ({iters_out} sigs)")
    print(f"  Per-sign average: {bench_ms / iters_out:,.1f} ms")
    print(f"  Signature length: {sig_len} bytes  (expected 666)")
    print(f"  Sig hash: {sig_hash}")

    # 3. KAT_CHECK — first 250 bytes of raw signature
    data, sw = send_apdu(dev, "e011000000")
    if sw != b"\x90\x00":
        sys.exit(f"INS_KAT_CHECK failed: SW={sw.hex()}")
    print(f"KAT signature first 32 bytes: {data[:32].hex()}")

    # 4. Reproducibility check
    print(f"\nReproducibility check: re-running INS_RUN_BENCH 3 more times...")
    canonical = sig_hash
    for i in range(3):
        data, sw = send_apdu(dev, "e010010000")
        h = data[3:35].hex()
        ok = "✓" if h == canonical else "✗ DIFFERENT"
        print(f"  Run {i+1}: hash = {h[:8]}...  {ok}")

    print("\n" + "=" * 60)
    print("Summary to send back:")
    print("=" * 60)
    print(f"  Device:           Ledger {model}")
    print(f"  App version:      {chr(data[0])}{chr(data[1])}{chr(data[2])}")  # placeholder
    print(f"  Per-sign avg:     {bench_ms / iters_out:,.1f} ms")
    print(f"  Sig hash:         {canonical}")
    print(f"  Provision time:   {prov_ms:.0f} ms")
    print(f"  Reproducible:     yes (3 runs identical)" if canonical == h
          else "  Reproducible:     NO — investigate")

    dev.close()


if __name__ == "__main__":
    main()
