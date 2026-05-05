#!/usr/bin/env python3
import socket, struct

def send_apdu(apdu_hex):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(60)
    s.connect(("localhost", 9999))
    apdu = bytes.fromhex(apdu_hex)
    s.sendall(struct.pack(">I", len(apdu)) + apdu)
    raw_len = s.recv(4)
    if len(raw_len) < 4: s.close(); return "FAIL_NO_LEN"
    n = struct.unpack(">I", raw_len)[0]
    rsp = b""
    while len(rsp) < n:
        c = s.recv(n - len(rsp))
        if not c: break
        rsp += c
    s.close()
    if len(rsp) >= 2:
        sw = rsp[-2:].hex()
        body = rsp[:-2].hex()
        return f"body={body!r:60} SW={sw}"
    return f"raw={rsp.hex()}"

print(f"GET_VERSION  → {send_apdu('e003000000')}")
print(f"GET_APP_NAME → {send_apdu('e004000000')}")
