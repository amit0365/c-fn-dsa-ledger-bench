#!/usr/bin/env python3
import socket, struct, time

def send(apdu_hex, label):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(120)
    s.connect(("localhost", 9999))
    apdu = bytes.fromhex(apdu_hex)
    t0 = time.monotonic()
    s.sendall(struct.pack(">I", len(apdu)) + apdu)
    raw_len = s.recv(4)
    n = struct.unpack(">I", raw_len)[0]
    rsp = b""
    while len(rsp) < n:
        c = s.recv(n - len(rsp))
        if not c: break
        rsp += c
    sw = s.recv(2)
    elapsed_ms = (time.monotonic() - t0) * 1000
    s.close()
    print(f"{label}")
    print(f"  → {apdu_hex}")
    print(f"  ← data ({n} bytes): {rsp.hex()[:80]}{'...' if n > 40 else ''}")
    print(f"  ← SW: {sw.hex()}   elapsed: {elapsed_ms:.0f} ms")
    return rsp, sw

# 1. Provision the basis (computes B from hardcoded sk, writes to NVRAM)
data, sw = send("e012000000", "INS_PROVISION (compute basis + nvm_write)")

# 2. Run a 1-iteration bench
data, sw = send("e010010000", "INS_RUN_BENCH (1 iter — sign once)")

# 3. KAT check (single sign, raw signature)  
data, sw = send("e011000000", "INS_KAT_CHECK (1 sign, raw sig)")
