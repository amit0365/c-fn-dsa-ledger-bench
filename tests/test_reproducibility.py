#!/usr/bin/env python3
import socket, struct
def send(apdu_hex):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(120); s.connect(("localhost", 9999))
    apdu = bytes.fromhex(apdu_hex)
    s.sendall(struct.pack(">I", len(apdu)) + apdu)
    n = struct.unpack(">I", s.recv(4))[0]
    rsp = b""
    while len(rsp) < n:
        c = s.recv(n - len(rsp))
        if not c: break
        rsp += c
    sw = s.recv(2); s.close()
    return rsp, sw

# Run bench 3 times — sig_hash must be identical (same sk + msg + seed)
for i in range(3):
    data, sw = send("e010010000")
    sig_len = int.from_bytes(data[1:3], "little")
    sig_hash = data[3:].hex()
    print(f"Run {i+1}: sig_len={sig_len}  sig_hash={sig_hash}  SW={sw.hex()}")
